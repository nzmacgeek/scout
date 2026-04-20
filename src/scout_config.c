#include "config.h"

#include "common.h"
#include "scout_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void scout_config_init_defaults(scout_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->interface, sizeof(cfg->interface), "eth0");
    snprintf(cfg->lease_file, sizeof(cfg->lease_file), "/var/lib/scout/lease");
    snprintf(cfg->resolv_conf, sizeof(cfg->resolv_conf), "/etc/resolv.conf");
    snprintf(cfg->interfaces_file, sizeof(cfg->interfaces_file), "/etc/interfaces");
    cfg->request_timeout = 5;
    cfg->retry_interval = 10;
    cfg->renew_margin = 60;
    cfg->max_retries = 0;
    cfg->persist_interfaces = 1;
}

static int scout_config_assign_string(char *dst, size_t dst_size, const char *value)
{
    if (snprintf(dst, dst_size, "%s", value) >= (int)dst_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int scout_config_load(const char *path, scout_config_t *cfg)
{
    FILE *fp;
    char line[1024];
    unsigned int lineno = 0;

    if (!path || !cfg) {
        errno = EINVAL;
        return -1;
    }

    scout_config_init_defaults(cfg);

    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *key;
        char *value;
        char *eq;
        lineno++;

        key = scout_trim(line);
        if (*key == '\0' || *key == '#') {
            continue;
        }

        eq = strchr(key, '=');
        if (!eq) {
            scout_log_message("WARN", "%s:%u: ignoring malformed line", path, lineno);
            continue;
        }

        *eq = '\0';
        value = scout_trim(eq + 1);
        key = scout_trim(key);

        if (strcmp(key, "interface") == 0) {
            if (scout_config_assign_string(cfg->interface, sizeof(cfg->interface), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "hostname") == 0) {
            if (scout_config_assign_string(cfg->hostname, sizeof(cfg->hostname), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "client_id") == 0) {
            if (scout_config_assign_string(cfg->client_id, sizeof(cfg->client_id), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "lease_file") == 0) {
            if (scout_config_assign_string(cfg->lease_file, sizeof(cfg->lease_file), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "resolv_conf") == 0) {
            if (scout_config_assign_string(cfg->resolv_conf, sizeof(cfg->resolv_conf), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "interfaces_file") == 0) {
            if (scout_config_assign_string(cfg->interfaces_file, sizeof(cfg->interfaces_file), value) != 0) {
                goto fail;
            }
        } else if (strcmp(key, "request_timeout") == 0) {
            if (scout_parse_u32(value, &cfg->request_timeout) != 0) {
                scout_log_message("WARN", "%s:%u: invalid request_timeout '%s'", path, lineno, value);
            }
        } else if (strcmp(key, "retry_interval") == 0) {
            if (scout_parse_u32(value, &cfg->retry_interval) != 0) {
                scout_log_message("WARN", "%s:%u: invalid retry_interval '%s'", path, lineno, value);
            }
        } else if (strcmp(key, "renew_margin") == 0) {
            if (scout_parse_u32(value, &cfg->renew_margin) != 0) {
                scout_log_message("WARN", "%s:%u: invalid renew_margin '%s'", path, lineno, value);
            }
        } else if (strcmp(key, "max_retries") == 0) {
            if (scout_parse_u32(value, &cfg->max_retries) != 0) {
                scout_log_message("WARN", "%s:%u: invalid max_retries '%s'", path, lineno, value);
            }
        } else if (strcmp(key, "persist_interfaces") == 0) {
            if (scout_parse_bool(value, &cfg->persist_interfaces) != 0) {
                scout_log_message("WARN", "%s:%u: invalid persist_interfaces '%s'", path, lineno, value);
            }
        } else {
            scout_log_message("WARN", "%s:%u: unknown key '%s'", path, lineno, key);
        }
    }

    fclose(fp);
    return 0;

fail:
    fclose(fp);
    return -1;
}
