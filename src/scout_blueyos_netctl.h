#ifndef SCOUT_BLUEYOS_NETCTL_H
#define SCOUT_BLUEYOS_NETCTL_H

#include <stdint.h>

#include "scout_types.h"

#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
int scout_blueyos_netctl_get_interface(const char *ifname, scout_iface_t *iface);
int scout_blueyos_netctl_set_link_up(unsigned int ifindex, uint32_t flags);
#else
static inline int scout_blueyos_netctl_get_interface(const char *ifname, scout_iface_t *iface)
{
    (void)ifname;
    (void)iface;
    return -1;
}

static inline int scout_blueyos_netctl_set_link_up(unsigned int ifindex, uint32_t flags)
{
    (void)ifindex;
    (void)flags;
    return -1;
}
#endif

#endif
