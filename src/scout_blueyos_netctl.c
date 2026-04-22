#include "config.h"

#include "common.h"
#include "scout_blueyos_netctl.h"

#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)

/* Compile-time debug tracing — always on for BlueyOS diagnostic builds */
#define NETCTL_DBG(fmt, ...) \
    fprintf(stderr, "[netctl dbg %s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#define BLUEY_AF_NETCTL 2
#define BLUEY_SOCK_NETCTL 3

#define NETCTL_PROTOCOL_VERSION 1
#define NETCTL_FLAG_REQUEST 0x0001

#define NETCTL_MSG_NETDEV_GET 12
#define NETCTL_MSG_NETDEV_SET 13
#define NETCTL_MSG_NETDEV_LIST 14
#define NETCTL_MSG_ADDR_NEW 20
#define NETCTL_MSG_ROUTE_NEW 30
#define NETCTL_MSG_DONE 2
#define NETCTL_MSG_ERROR 1

#define NETCTL_ATTR_NESTED 1
#define NETCTL_ATTR_IFINDEX 10
#define NETCTL_ATTR_IFNAME 11
#define NETCTL_ATTR_MTU 12
#define NETCTL_ATTR_MAC 13
#define NETCTL_ATTR_FLAGS 14
#define NETCTL_ATTR_CARRIER 15
#define NETCTL_ATTR_ADDR_FAMILY 20
#define NETCTL_ATTR_ADDR_VALUE 21
#define NETCTL_ATTR_ADDR_PREFIX 22
#define NETCTL_ATTR_ROUTE_DST 30
#define NETCTL_ATTR_ROUTE_GW 31
#define NETCTL_ATTR_ROUTE_OIF 32
#define NETCTL_ATTR_ROUTE_METRIC 33
#define NETCTL_ATTR_ROUTE_PREFIX 34
#define NETCTL_ATTR_ROUTE_FAMILY 35

#define NETCTL_AF_INET 2

#define NETCTL_FLAG_UP 0x0001

typedef struct {
    uint32_t msg_len;
    uint16_t msg_type;
    uint16_t msg_flags;
    uint32_t msg_seq;
    uint32_t msg_pid;
    uint16_t msg_version;
    uint16_t msg_reserved;
} scout_netctl_header_t;

typedef struct {
    uint16_t attr_len;
    uint16_t attr_type;
} scout_netctl_attr_t;

static size_t scout_netctl_attr_align(size_t len)
{
    return (len + 3u) & ~3u;
}

static void scout_netctl_init(void *buf, uint16_t type, uint32_t seq)
{
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)buf;

    memset(buf, 0, sizeof(*hdr));
    hdr->msg_len = sizeof(*hdr);
    hdr->msg_type = type;
    hdr->msg_flags = NETCTL_FLAG_REQUEST;
    hdr->msg_seq = seq;
    hdr->msg_pid = (uint32_t)getpid();
    hdr->msg_version = NETCTL_PROTOCOL_VERSION;
}

static int scout_netctl_add_attr(void *msg, size_t msg_size, uint16_t type, const void *data, uint16_t len)
{
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)msg;
    scout_netctl_attr_t *attr;
    size_t attr_size = scout_netctl_attr_align(sizeof(*attr) + len);

    if (hdr->msg_len + attr_size > msg_size) {
        errno = ENOSPC;
        return -1;
    }

    attr = (scout_netctl_attr_t *)((uint8_t *)msg + hdr->msg_len);
    attr->attr_len = (uint16_t)(sizeof(*attr) + len);
    attr->attr_type = type;
    memcpy((uint8_t *)attr + sizeof(*attr), data, len);
    memset((uint8_t *)attr + attr->attr_len, 0, attr_size - attr->attr_len);
    hdr->msg_len += (uint32_t)attr_size;
    return 0;
}

static int scout_netctl_open(void)
{
    int fd;
#ifdef SYS_socket
    NETCTL_DBG("scout_netctl_open: syscall(SYS_socket, AF_NETCTL=%d, SOCK_NETCTL=%d, 0)", BLUEY_AF_NETCTL, BLUEY_SOCK_NETCTL);
    fd = (int)syscall(SYS_socket, BLUEY_AF_NETCTL, BLUEY_SOCK_NETCTL, 0);
#else
    long args[6] = { BLUEY_AF_NETCTL, BLUEY_SOCK_NETCTL, 0, 0, 0, 0 };
    NETCTL_DBG("scout_netctl_open: syscall(SYS_socketcall, AF_NETCTL=%d, SOCK_NETCTL=%d)", BLUEY_AF_NETCTL, BLUEY_SOCK_NETCTL);
    fd = (int)syscall(SYS_socketcall, 1, args);
#endif
    NETCTL_DBG("scout_netctl_open: fd=%d errno=%d (%s)", fd, errno, strerror(errno));
    return fd;
}

static int scout_netctl_exchange(int fd, void *req, size_t req_len, void *resp, size_t resp_len)
{
    struct iovec iov;
    struct msghdr msg;
    ssize_t sent;
    ssize_t got;

    NETCTL_DBG("scout_netctl_exchange: fd=%d req_len=%zu resp_len=%zu", fd, req_len, resp_len);

    memset(&iov, 0, sizeof(iov));
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = req;
    iov.iov_len = req_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    sent = sendmsg(fd, &msg, 0);
    if (sent < 0) {
        NETCTL_DBG("scout_netctl_exchange: sendmsg FAILED errno=%d (%s)", errno, strerror(errno));
        return -1;
    }
    NETCTL_DBG("scout_netctl_exchange: sendmsg OK sent=%zd", sent);

    iov.iov_base = resp;
    iov.iov_len = resp_len;
    got = recvmsg(fd, &msg, 0);
    if (got < 0) {
        NETCTL_DBG("scout_netctl_exchange: recvmsg FAILED errno=%d (%s)", errno, strerror(errno));
        return -1;
    }
    NETCTL_DBG("scout_netctl_exchange: recvmsg OK got=%zd", got);

    return (int)got;
}

static int scout_netctl_parse_iface_attrs(const scout_netctl_attr_t *attr, size_t len, scout_iface_t *iface)
{
    const uint8_t *cur = (const uint8_t *)attr;
    size_t remaining = len;

    while (remaining >= sizeof(*attr)) {
        const scout_netctl_attr_t *a = (const scout_netctl_attr_t *)cur;
        const uint8_t *payload = cur + sizeof(*a);
        size_t payload_len;
        size_t aligned_len;

        if (a->attr_len < sizeof(*a) || a->attr_len > remaining) {
            break;
        }

        payload_len = a->attr_len - sizeof(*a);
        aligned_len = scout_netctl_attr_align(a->attr_len);

        switch (a->attr_type) {
        case NETCTL_ATTR_IFINDEX:
            if (payload_len >= sizeof(uint32_t)) {
                memcpy(&iface->ifindex, payload, sizeof(uint32_t));
            }
            break;
        case NETCTL_ATTR_IFNAME:
            snprintf(iface->name, sizeof(iface->name), "%s", (const char *)payload);
            break;
        case NETCTL_ATTR_MTU:
            if (payload_len >= sizeof(uint32_t)) {
                memcpy(&iface->mtu, payload, sizeof(uint32_t));
            }
            break;
        case NETCTL_ATTR_MAC:
            if (payload_len >= 6) {
                memcpy(iface->mac, payload, 6);
            }
            break;
        case NETCTL_ATTR_FLAGS:
            if (payload_len >= sizeof(uint32_t)) {
                memcpy(&iface->flags, payload, sizeof(uint32_t));
            }
            break;
        case NETCTL_ATTR_CARRIER:
            if (payload_len >= 1) {
                iface->carrier = payload[0] ? 1 : 0;
            }
            break;
        default:
            break;
        }

        if (aligned_len > remaining) {
            break;
        }
        cur += aligned_len;
        remaining -= aligned_len;
    }

    return 0;
}

int scout_blueyos_netctl_get_interface(const char *ifname, scout_iface_t *iface)
{
    uint8_t req[256];
    uint8_t resp[2048];
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)resp;
    const uint8_t *cur;
    size_t remaining;
    int fd;
    int rc = -1;

    NETCTL_DBG("get_interface: ifname=%s", ifname ? ifname : "(null)");

    if (!ifname || !iface) {
        errno = EINVAL;
        NETCTL_DBG("get_interface: EINVAL");
        return -1;
    }

    memset(iface, 0, sizeof(*iface));

    fd = scout_netctl_open();
    if (fd < 0) {
        NETCTL_DBG("get_interface: netctl_open FAILED fd=%d errno=%d", fd, errno);
        return -1;
    }
    NETCTL_DBG("get_interface: netctl_open OK fd=%d", fd);

    scout_netctl_init(req, NETCTL_MSG_NETDEV_LIST, 1);
    rc = scout_netctl_exchange(fd, req, sizeof(scout_netctl_header_t), resp, sizeof(resp));
    NETCTL_DBG("get_interface: exchange rc=%d hdr->msg_type=%u hdr->msg_len=%u",
               rc, rc >= (int)sizeof(*hdr) ? hdr->msg_type : 0xFFFF,
               rc >= (int)sizeof(*hdr) ? hdr->msg_len : 0);
    if (rc < (int)sizeof(*hdr)) {
        close(fd);
        errno = EIO;
        NETCTL_DBG("get_interface: exchange response too short (rc=%d need=%zu)", rc, sizeof(*hdr));
        return -1;
    }

    if (hdr->msg_type == NETCTL_MSG_ERROR) {
        close(fd);
        errno = EIO;
        NETCTL_DBG("get_interface: NETCTL_MSG_ERROR returned by kernel");
        return -1;
    }

    cur = resp + sizeof(*hdr);
    remaining = hdr->msg_len - sizeof(*hdr);
    NETCTL_DBG("get_interface: scanning %zu bytes of attrs for '%s'", remaining, ifname);

    while (remaining >= sizeof(scout_netctl_attr_t)) {
        const scout_netctl_attr_t *attr = (const scout_netctl_attr_t *)cur;
        scout_iface_t candidate;
        size_t payload_len;
        size_t aligned_len;

        if (attr->attr_len < sizeof(*attr) || attr->attr_len > remaining) {
            NETCTL_DBG("get_interface: attr_len=%u malformed, stopping", attr->attr_len);
            break;
        }

        memset(&candidate, 0, sizeof(candidate));
        payload_len = attr->attr_len - sizeof(*attr);
        aligned_len = scout_netctl_attr_align(attr->attr_len);

        if (attr->attr_type == NETCTL_ATTR_NESTED) {
            scout_netctl_parse_iface_attrs((const scout_netctl_attr_t *)(cur + sizeof(*attr)), payload_len, &candidate);
            NETCTL_DBG("get_interface: found iface '%s' (looking for '%s')", candidate.name, ifname);
            if (strcmp(candidate.name, ifname) == 0) {
                *iface = candidate;
                close(fd);
                NETCTL_DBG("get_interface: match found, returning OK");
                return 0;
            }
        }

        if (aligned_len > remaining) {
            break;
        }
        cur += aligned_len;
        remaining -= aligned_len;
    }

    close(fd);
    errno = ENODEV;
    NETCTL_DBG("get_interface: '%s' not found in kernel netdev list, errno=ENODEV", ifname);
    return -1;
}

int scout_blueyos_netctl_set_link_up(unsigned int ifindex, uint32_t flags)
{
    uint8_t req[256];
    uint8_t resp[256];
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)resp;
    int fd;
    int rc;
    uint32_t new_flags = flags | NETCTL_FLAG_UP;

    fd = scout_netctl_open();
    if (fd < 0) {
        return -1;
    }

    scout_netctl_init(req, NETCTL_MSG_NETDEV_SET, 2);
    if (scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_IFINDEX, &ifindex, sizeof(ifindex)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_FLAGS, &new_flags, sizeof(new_flags)) != 0) {
        close(fd);
        return -1;
    }

    rc = scout_netctl_exchange(fd, req, ((scout_netctl_header_t *)req)->msg_len, resp, sizeof(resp));
    close(fd);
    if (rc < (int)sizeof(*hdr) || hdr->msg_type == NETCTL_MSG_ERROR) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int scout_blueyos_netctl_addr_add(unsigned int ifindex, uint32_t addr, uint8_t prefix_len)
{
    uint8_t req[256];
    uint8_t resp[256];
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)resp;
    uint16_t family = NETCTL_AF_INET;
    int fd;
    int rc;

    fd = scout_netctl_open();
    if (fd < 0) {
        return -1;
    }

    scout_netctl_init(req, NETCTL_MSG_ADDR_NEW, 3);
    if (scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_IFINDEX, &ifindex, sizeof(ifindex)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ADDR_FAMILY, &family, sizeof(family)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ADDR_VALUE, &addr, sizeof(addr)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ADDR_PREFIX, &prefix_len, sizeof(prefix_len)) != 0) {
        close(fd);
        return -1;
    }

    rc = scout_netctl_exchange(fd, req, ((scout_netctl_header_t *)req)->msg_len, resp, sizeof(resp));
    close(fd);
    if (rc < (int)sizeof(*hdr) || hdr->msg_type == NETCTL_MSG_ERROR) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int scout_blueyos_netctl_route_add(unsigned int ifindex, uint32_t dst, uint8_t prefix_len, uint32_t gw, uint32_t metric)
{
    uint8_t req[256];
    uint8_t resp[256];
    scout_netctl_header_t *hdr = (scout_netctl_header_t *)resp;
    uint16_t family = NETCTL_AF_INET;
    int fd;
    int rc;

    fd = scout_netctl_open();
    if (fd < 0) {
        return -1;
    }

    scout_netctl_init(req, NETCTL_MSG_ROUTE_NEW, 4);
    if (scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_FAMILY, &family, sizeof(family)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_DST, &dst, sizeof(dst)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_PREFIX, &prefix_len, sizeof(prefix_len)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_GW, &gw, sizeof(gw)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_OIF, &ifindex, sizeof(ifindex)) != 0 ||
        scout_netctl_add_attr(req, sizeof(req), NETCTL_ATTR_ROUTE_METRIC, &metric, sizeof(metric)) != 0) {
        close(fd);
        return -1;
    }

    rc = scout_netctl_exchange(fd, req, ((scout_netctl_header_t *)req)->msg_len, resp, sizeof(resp));
    close(fd);
    if (rc < (int)sizeof(*hdr) || hdr->msg_type == NETCTL_MSG_ERROR) {
        errno = EIO;
        return -1;
    }

    return 0;
}

#endif
