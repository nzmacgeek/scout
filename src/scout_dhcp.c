#include "config.h"

#include "common.h"
#include "scout_dhcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef SO_BINDTODEVICE
#include <net/if.h>
#endif

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363u

#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY 2

#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET 6

#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_HOSTNAME 12
#define DHCP_OPTION_DOMAIN_NAME 15
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAMETER_REQUEST 55
#define DHCP_OPTION_RENEWAL_TIME 58
#define DHCP_OPTION_REBIND_TIME 59
#define DHCP_OPTION_CLIENT_ID 61
#define DHCP_OPTION_END 255

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5

struct dhcp_packet {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
};

static int scout_dhcp_open_socket(const scout_iface_t *iface)
{
    int fd;
    int yes = 1;
    struct sockaddr_in sin;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) != 0) {
        close(fd);
        return -1;
    }

#ifdef SO_BINDTODEVICE
    if (iface && iface->name[0] != '\0') {
        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface->name, (socklen_t)strlen(iface->name)) != 0) {
            scout_log_message("WARN", "unable to bind DHCP socket to %s: %s", iface->name, strerror(errno));
        }
    }
#endif

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DHCP_CLIENT_PORT);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static size_t scout_dhcp_add_option(uint8_t *dst, size_t offset, size_t max_len, uint8_t code, const void *data, uint8_t len)
{
    if (offset + 2u + len > max_len) {
        return max_len;
    }
    dst[offset++] = code;
    dst[offset++] = len;
    memcpy(dst + offset, data, len);
    return offset + len;
}

static size_t scout_dhcp_build_packet(struct dhcp_packet *pkt,
                                      const scout_config_t *cfg,
                                      const scout_iface_t *iface,
                                      uint32_t xid,
                                      uint8_t msg_type,
                                      const scout_lease_t *lease)
{
    static const uint8_t params[] = {
        DHCP_OPTION_SUBNET_MASK,
        DHCP_OPTION_ROUTER,
        DHCP_OPTION_DNS,
        DHCP_OPTION_DOMAIN_NAME,
        DHCP_OPTION_LEASE_TIME,
        DHCP_OPTION_RENEWAL_TIME,
        DHCP_OPTION_REBIND_TIME
    };
    size_t opt = 0;
    uint8_t client_id[1 + DHCP_HLEN_ETHERNET];

    memset(pkt, 0, sizeof(*pkt));
    pkt->op = DHCP_OP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHERNET;
    pkt->hlen = DHCP_HLEN_ETHERNET;
    pkt->xid = htonl(xid);
    pkt->flags = htons(0x8000u);
    memcpy(pkt->chaddr, iface->mac, 6);
    pkt->magic = htonl(DHCP_MAGIC_COOKIE);

    opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_MESSAGE_TYPE, &msg_type, 1);
    opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_PARAMETER_REQUEST, params, sizeof(params));

    if (cfg->hostname[0] != '\0') {
        opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_HOSTNAME,
                                    cfg->hostname, (uint8_t)strlen(cfg->hostname));
    }

    if (cfg->client_id[0] != '\0') {
        opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_CLIENT_ID,
                                    cfg->client_id, (uint8_t)strlen(cfg->client_id));
    } else {
        client_id[0] = DHCP_HTYPE_ETHERNET;
        memcpy(client_id + 1, iface->mac, 6);
        opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id));
    }

    if (lease && lease->address != 0) {
        uint32_t requested_ip = htonl(lease->address);
        uint32_t server_id = htonl(lease->server_id);
        opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_REQUESTED_IP,
                                    &requested_ip, sizeof(requested_ip));
        if (lease->server_id != 0) {
            opt = scout_dhcp_add_option(pkt->options, opt, sizeof(pkt->options), DHCP_OPTION_SERVER_ID,
                                        &server_id, sizeof(server_id));
        }
    }

    if (opt < sizeof(pkt->options)) {
        pkt->options[opt++] = DHCP_OPTION_END;
    }
    return offsetof(struct dhcp_packet, options) + opt;
}

int scout_dhcp_parse_options(const uint8_t *options, size_t options_len, scout_lease_t *lease_out, uint8_t *message_type_out)
{
    size_t i = 0;

    while (i < options_len) {
        uint8_t code = options[i++];
        uint8_t len;
        uint8_t option_len;

        if (code == DHCP_OPTION_END) {
            break;
        }
        if (code == 0) {
            continue;
        }
        if (i >= options_len) {
            break;
        }

        len = options[i++];
        option_len = len;
        if (i + option_len > options_len) {
            break;
        }

        switch (code) {
        case DHCP_OPTION_MESSAGE_TYPE:
            if (len == 1 && message_type_out) {
                *message_type_out = options[i];
            }
            break;
        case DHCP_OPTION_SUBNET_MASK:
            if (len == 4) {
                memcpy(&lease_out->subnet_mask, options + i, 4);
                lease_out->subnet_mask = ntohl(lease_out->subnet_mask);
            }
            break;
        case DHCP_OPTION_ROUTER:
            if (len >= 4) {
                memcpy(&lease_out->router, options + i, 4);
                lease_out->router = ntohl(lease_out->router);
            }
            break;
        case DHCP_OPTION_DNS:
            lease_out->dns_count = 0;
            while (len >= 4 && lease_out->dns_count < SCOUT_MAX_DNS_SERVERS) {
                uint32_t dns_addr;
                memcpy(&dns_addr, options + i + lease_out->dns_count * 4u, 4);
                lease_out->dns[lease_out->dns_count++] = ntohl(dns_addr);
                len -= 4;
            }
            break;
        case DHCP_OPTION_LEASE_TIME:
            if (len == 4) {
                memcpy(&lease_out->lease_time, options + i, 4);
                lease_out->lease_time = ntohl(lease_out->lease_time);
            }
            break;
        case DHCP_OPTION_SERVER_ID:
            if (len == 4) {
                memcpy(&lease_out->server_id, options + i, 4);
                lease_out->server_id = ntohl(lease_out->server_id);
            }
            break;
        case DHCP_OPTION_RENEWAL_TIME:
            if (len == 4) {
                memcpy(&lease_out->renewal_time, options + i, 4);
                lease_out->renewal_time = ntohl(lease_out->renewal_time);
            }
            break;
        case DHCP_OPTION_REBIND_TIME:
            if (len == 4) {
                memcpy(&lease_out->rebind_time, options + i, 4);
                lease_out->rebind_time = ntohl(lease_out->rebind_time);
            }
            break;
        case DHCP_OPTION_DOMAIN_NAME:
            if (len >= sizeof(lease_out->domain)) {
                len = sizeof(lease_out->domain) - 1;
            }
            memcpy(lease_out->domain, options + i, len);
            lease_out->domain[len] = '\0';
            break;
        default:
            break;
        }

        i += option_len;
    }

    if (lease_out->renewal_time == 0 && lease_out->lease_time > 0) {
        lease_out->renewal_time = lease_out->lease_time / 2u;
    }
    if (lease_out->rebind_time == 0 && lease_out->lease_time > 0) {
        lease_out->rebind_time = (lease_out->lease_time * 7u) / 8u;
    }

    return 0;
}

static int scout_dhcp_receive(int fd, uint32_t xid, uint8_t expected_type, uint32_t timeout_secs, scout_lease_t *lease_out)
{
    struct timeval tv;
    fd_set rfds;

    while (1) {
        struct dhcp_packet pkt;
        ssize_t nread;
        uint8_t msg_type = 0;

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = (time_t)timeout_secs;
        tv.tv_usec = 0;

        if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        nread = recv(fd, &pkt, sizeof(pkt), 0);
        if (nread < (ssize_t)(offsetof(struct dhcp_packet, options) + 4)) {
            continue;
        }
        if (pkt.op != DHCP_OP_BOOTREPLY || ntohl(pkt.xid) != xid || ntohl(pkt.magic) != DHCP_MAGIC_COOKIE) {
            continue;
        }

        memset(lease_out, 0, sizeof(*lease_out));
        lease_out->address = ntohl(pkt.yiaddr);
        scout_dhcp_parse_options(pkt.options, (size_t)nread - offsetof(struct dhcp_packet, options), lease_out, &msg_type);
        if (msg_type != expected_type) {
            continue;
        }
        lease_out->obtained_at = time(NULL);
        return 0;
    }
}

static int scout_dhcp_send_packet(int fd, const struct dhcp_packet *pkt, size_t pkt_len, uint32_t destination)
{
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DHCP_SERVER_PORT);
    sin.sin_addr.s_addr = htonl(destination);
    return sendto(fd, pkt, pkt_len, 0, (struct sockaddr *)&sin, sizeof(sin)) >= 0 ? 0 : -1;
}

int scout_dhcp_acquire_lease(const scout_config_t *cfg,
                             const scout_iface_t *iface,
                             const scout_lease_t *previous,
                             int renew,
                             scout_lease_t *lease_out)
{
    struct dhcp_packet pkt;
    scout_lease_t offer;
    int fd;
    uint32_t xid;
    size_t pkt_len;
    int rc = -1;

    if (!cfg || !iface || !lease_out) {
        errno = EINVAL;
        return -1;
    }

    xid = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16) ^ iface->ifindex;
    fd = scout_dhcp_open_socket(iface);
    if (fd < 0) {
        return -1;
    }

    pkt_len = scout_dhcp_build_packet(&pkt, cfg, iface, xid, renew ? DHCP_REQUEST : DHCP_DISCOVER, renew ? previous : NULL);
    if (scout_dhcp_send_packet(fd, &pkt, pkt_len, INADDR_BROADCAST) != 0) {
        close(fd);
        return -1;
    }

    if (!renew) {
        if (scout_dhcp_receive(fd, xid, DHCP_OFFER, cfg->request_timeout, &offer) != 0) {
            close(fd);
            return -1;
        }

        pkt_len = scout_dhcp_build_packet(&pkt, cfg, iface, xid + 1u, DHCP_REQUEST, &offer);
        xid += 1u;
        if (scout_dhcp_send_packet(fd, &pkt, pkt_len, INADDR_BROADCAST) != 0) {
            close(fd);
            return -1;
        }
    }

    rc = scout_dhcp_receive(fd, xid, DHCP_ACK, cfg->request_timeout, lease_out);
    close(fd);
    return rc;
}
