#ifndef SCOUT_BLUEYOS_NETCTL_H
#define SCOUT_BLUEYOS_NETCTL_H

#include <stdint.h>

#include "scout_types.h"

#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
int scout_blueyos_netctl_get_interface(const char *ifname, scout_iface_t *iface);
int scout_blueyos_netctl_list_interfaces(int (*cb)(const scout_iface_t *, void *), void *userdata);
int scout_blueyos_netctl_set_link_up(unsigned int ifindex, uint32_t flags);
int scout_blueyos_netctl_set_link_down(unsigned int ifindex, uint32_t flags);
int scout_blueyos_netctl_addr_add(unsigned int ifindex, uint32_t addr, uint8_t prefix_len);
int scout_blueyos_netctl_route_add(unsigned int ifindex, uint32_t dst, uint8_t prefix_len, uint32_t gw, uint32_t metric);
#else
static inline int scout_blueyos_netctl_get_interface(const char *ifname, scout_iface_t *iface)
{
    (void)ifname;
    (void)iface;
    return -1;
}

static inline int scout_blueyos_netctl_list_interfaces(int (*cb)(const scout_iface_t *, void *), void *userdata)
{
    (void)cb;
    (void)userdata;
    return -1;
}

static inline int scout_blueyos_netctl_set_link_up(unsigned int ifindex, uint32_t flags)
{
    (void)ifindex;
    (void)flags;
    return -1;
}

static inline int scout_blueyos_netctl_set_link_down(unsigned int ifindex, uint32_t flags)
{
    (void)ifindex;
    (void)flags;
    return -1;
}

static inline int scout_blueyos_netctl_addr_add(unsigned int ifindex, uint32_t addr, uint8_t prefix_len)
{
    (void)ifindex;
    (void)addr;
    (void)prefix_len;
    return -1;
}

static inline int scout_blueyos_netctl_route_add(unsigned int ifindex, uint32_t dst, uint8_t prefix_len, uint32_t gw, uint32_t metric)
{
    (void)ifindex;
    (void)dst;
    (void)prefix_len;
    (void)gw;
    (void)metric;
    return -1;
}
#endif

#endif
