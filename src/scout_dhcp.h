#ifndef SCOUT_DHCP_H
#define SCOUT_DHCP_H

#include "scout_config.h"
#include "scout_types.h"

int scout_dhcp_acquire_lease(const scout_config_t *cfg,
                             const scout_iface_t *iface,
                             const scout_lease_t *previous,
                             int renew,
                             scout_lease_t *lease_out);

int scout_dhcp_parse_options(const uint8_t *options, size_t options_len, scout_lease_t *lease_out, uint8_t *message_type_out);

#endif
