#ifndef SCOUT_PLATFORM_H
#define SCOUT_PLATFORM_H

#include "scout_types.h"

int scout_platform_get_interface(const char *ifname, scout_iface_t *iface);
int scout_platform_set_link_up(const scout_iface_t *iface);
int scout_platform_apply_lease(const scout_iface_t *iface, const scout_lease_t *lease, int *applied_live);
int scout_platform_raw_diag_supported(void);
const char *scout_platform_raw_diag_reason(void);

#endif
