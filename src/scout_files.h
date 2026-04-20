#ifndef SCOUT_FILES_H
#define SCOUT_FILES_H

#include "scout_types.h"

int scout_write_lease_file(const char *path, const scout_iface_t *iface, const scout_lease_t *lease);
int scout_write_resolv_conf(const char *path, const scout_lease_t *lease);
int scout_write_interfaces_snapshot(const char *path, const scout_iface_t *iface, const scout_lease_t *lease);

#endif
