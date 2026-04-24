#include "config.h"

#include "common.h"
#include "scout_blueyos_netctl.h"
#include "scout_platform.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
#include <arpa/inet.h>
#include <net/if.h>
#include <net/route.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#else
#include "common.h"
#endif

static const char *g_raw_diag_reason = "";

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
static void scout_platform_copy_ifname(struct ifreq *ifr, const char *ifname)
{
    memset(ifr->ifr_name, 0, sizeof(ifr->ifr_name));
    strncpy(ifr->ifr_name, ifname, sizeof(ifr->ifr_name) - 1);
}
#endif

int scout_platform_get_interface(const char *ifname, scout_iface_t *iface)
{
#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    return scout_blueyos_netctl_get_interface(ifname, iface);
#else
    int fd;
    struct ifreq ifr;

    if (!ifname || !iface) {
        errno = EINVAL;
        return -1;
    }

    memset(iface, 0, sizeof(*iface));
    snprintf(iface->name, sizeof(iface->name), "%s", ifname);

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    scout_platform_copy_ifname(&ifr, ifname);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) != 0) {
        close(fd);
        return -1;
    }
    iface->ifindex = (unsigned int)ifr.ifr_ifindex;

#ifdef SIOCGIFHWADDR
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(iface->mac, ifr.ifr_hwaddr.sa_data, 6);
    }
#endif

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        iface->flags = (uint32_t)ifr.ifr_flags;
    }

#ifdef SIOCGIFMTU
    if (ioctl(fd, SIOCGIFMTU, &ifr) == 0) {
        iface->mtu = (uint32_t)ifr.ifr_mtu;
    }
#endif

    close(fd);
    return 0;
#endif
}

int scout_platform_set_link_up(const scout_iface_t *iface)
{
#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    return scout_blueyos_netctl_set_link_up(iface->ifindex, iface->flags);
#else
    int fd;
    struct ifreq ifr;

    if (!iface) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    scout_platform_copy_ifname(&ifr, iface->name);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
        close(fd);
        return -1;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
#endif
}

int scout_platform_set_link_down(const scout_iface_t *iface)
{
#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    return scout_blueyos_netctl_set_link_down(iface->ifindex, iface->flags);
#else
    int fd;
    struct ifreq ifr;

    if (!iface) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    scout_platform_copy_ifname(&ifr, iface->name);
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
        close(fd);
        return -1;
    }

    ifr.ifr_flags &= (short)~IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
#endif
}

int scout_platform_list_interfaces(int (*cb)(const scout_iface_t *, void *), void *userdata)
{
#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    return scout_blueyos_netctl_list_interfaces(cb, userdata);
#elif defined(HAVE_IF_NAMEINDEX)
    struct if_nameindex *list;
    struct if_nameindex *item;
    scout_iface_t iface;

    list = if_nameindex();
    if (!list) {
        return -1;
    }

    for (item = list; item && item->if_index != 0 && item->if_name; ++item) {
        if (scout_platform_get_interface(item->if_name, &iface) == 0) {
            if (cb(&iface, userdata) != 0) {
                break;
            }
        }
    }

    if_freenameindex(list);
    return 0;
#else
    (void)cb;
    (void)userdata;
    errno = ENOTSUP;
    return -1;
#endif
}

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
static int scout_platform_set_sockaddr(struct sockaddr_in *sin, uint32_t addr)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(addr);
    return 0;
}

static int scout_platform_set_ipv4_addr(const char *ifname, uint32_t addr, uint32_t netmask)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_in sin;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    scout_platform_copy_ifname(&ifr, ifname);

    scout_platform_set_sockaddr(&sin, addr);
    memcpy(&ifr.ifr_addr, &sin, sizeof(sin));
    if (ioctl(fd, SIOCSIFADDR, &ifr) != 0) {
        close(fd);
        return -1;
    }

    scout_platform_set_sockaddr(&sin, netmask);
    memcpy(&ifr.ifr_netmask, &sin, sizeof(sin));
    if (ioctl(fd, SIOCSIFNETMASK, &ifr) != 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int scout_platform_set_default_route(const char *ifname, uint32_t gateway)
{
    int fd;
    struct rtentry route;
    struct sockaddr_in sin;

    if (gateway == 0) {
        return 0;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&route, 0, sizeof(route));
    scout_platform_set_sockaddr(&sin, 0);
    memcpy(&route.rt_dst, &sin, sizeof(sin));
    memcpy(&route.rt_genmask, &sin, sizeof(sin));
    scout_platform_set_sockaddr(&sin, gateway);
    memcpy(&route.rt_gateway, &sin, sizeof(sin));
    route.rt_flags = RTF_UP | RTF_GATEWAY;
    route.rt_dev = (char *)ifname;

    ioctl(fd, SIOCDELRT, &route);
    if (ioctl(fd, SIOCADDRT, &route) != 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}
#endif

int scout_platform_apply_lease(const scout_iface_t *iface, const scout_lease_t *lease, int *applied_live)
{
    if (applied_live) {
        *applied_live = 0;
    }

    if (!iface || !lease) {
        errno = EINVAL;
        return -1;
    }

#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    uint8_t prefix_len = (uint8_t)scout_netmask_to_prefix(lease->subnet_mask);

    if (scout_blueyos_netctl_addr_add(iface->ifindex, lease->address, prefix_len) != 0) {
        return -1;
    }

    if (lease->router != 0 &&
        scout_blueyos_netctl_route_add(iface->ifindex, 0, 0, lease->router, 0) != 0) {
        return -1;
    }

    if (applied_live) {
        *applied_live = 1;
    }
    return 0;
#else
    if (scout_platform_set_ipv4_addr(iface->name, lease->address, lease->subnet_mask) != 0) {
        return -1;
    }
    if (scout_platform_set_default_route(iface->name, lease->router) != 0) {
        return -1;
    }
    if (applied_live) {
        *applied_live = 1;
    }
    return 0;
#endif
}

int scout_platform_raw_diag_supported(void)
{
    return 1;
}

const char *scout_platform_raw_diag_reason(void)
{
    return g_raw_diag_reason;
}
