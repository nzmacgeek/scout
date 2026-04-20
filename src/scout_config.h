#ifndef SCOUT_CONFIG_H
#define SCOUT_CONFIG_H

#include <stdint.h>

#include "common.h"

typedef struct {
    char interface[32];
    char hostname[64];
    char client_id[64];
    char lease_file[PATH_MAX];
    char resolv_conf[PATH_MAX];
    char interfaces_file[PATH_MAX];
    uint32_t request_timeout;
    uint32_t retry_interval;
    uint32_t renew_margin;
    uint32_t max_retries;
    int persist_interfaces;
} scout_config_t;

void scout_config_init_defaults(scout_config_t *cfg);
int scout_config_load(const char *path, scout_config_t *cfg);

#endif
