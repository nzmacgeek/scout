#include "config.h"

#include "common.h"
#include "scout_config.h"
#include "scout_dhcp.h"
#include "scout_files.h"
#include "scout_platform.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Compile-time debug tracing — enabled only when SCOUT_TRACE is defined */
#ifdef SCOUT_TRACE
#define SCOUT_DBG(fmt, ...) \
    scout_log_message("DEBUG", "[scoutd dbg %s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define SCOUT_DBG(fmt, ...) ((void)0)
#endif

static volatile sig_atomic_t g_stop = 0;
static volatile sig_atomic_t g_reload = 0;

static void scout_signal_handler(int signo)
{
    if (signo == SIGHUP) {
        g_reload = 1;
        return;
    }
    g_stop = 1;
}

static void scout_install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = scout_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void scout_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: scoutd [-n] [-1] [-c FILE] [--version]\n"
            "  -n        compatibility flag; scoutd already runs in the foreground\n"
            "  -1        acquire one lease and exit\n"
            "  -c FILE   configuration file (default: /etc/scout/scout.conf)\n"
            "  --version show version information\n");
}

static int scout_refresh_outputs(const scout_config_t *cfg,
                                 const scout_iface_t *iface,
                                 const scout_lease_t *lease)
{
    int applied_live = 0;

    if (scout_write_lease_file(cfg->lease_file, iface, lease) != 0) {
        scout_log_errno("ERROR", "writing lease file");
        return -1;
    }
    if (scout_write_resolv_conf(cfg->resolv_conf, lease) != 0) {
        scout_log_errno("ERROR", "writing resolv.conf");
        return -1;
    }
    if (cfg->persist_interfaces &&
        scout_write_interfaces_snapshot(cfg->interfaces_file, iface, lease) != 0) {
        scout_log_errno("WARN", "writing /etc/interfaces snapshot");
    }

    if (scout_platform_apply_lease(iface, lease, &applied_live) != 0) {
        scout_log_errno("WARN", "applying live lease settings");
    } else if (!applied_live) {
        scout_log_message("WARN", "live IPv4 address/route programming is not available on this build; resolver and persisted lease files were updated");
    }

    return 0;
}

int main(int argc, char **argv)
{
    scout_config_t cfg;
    scout_iface_t iface;
    scout_lease_t current_lease;
    const char *config_path = "/etc/scout/scout.conf";
    unsigned int attempts = 0;
    int oneshot = 0;
    int opt;

    SCOUT_DBG("main: scoutd starting, argc=%d", argc);
    scout_set_program_name("scoutd");

    while ((opt = getopt(argc, argv, "1nc:h")) != -1) {
        switch (opt) {
        case '1':
            oneshot = 1;
            break;
        case 'n':
            break;
        case 'c':
            config_path = optarg;
            break;
        case 'h':
            scout_usage(stdout);
            return 0;
        default:
            scout_usage(stderr);
            return 2;
        }
    }

    if (argc > 1 && strcmp(argv[argc - 1], "--version") == 0) {
        printf("scoutd %s\n", PACKAGE_VERSION);
        return 0;
    }

    SCOUT_DBG("main: loading config from %s", config_path);
    if (scout_config_load(config_path, &cfg) != 0) {
        scout_log_errno("ERROR", "loading configuration");
        SCOUT_DBG("main: scout_config_load FAILED errno=%d (%s)", errno, strerror(errno));
        return 1;
    }
    SCOUT_DBG("main: config loaded, interface=%s lease_file=%s", cfg.interface, cfg.lease_file);

    if (cfg.hostname[0] != '\0') {
        if (sethostname(cfg.hostname, strlen(cfg.hostname)) != 0) {
            scout_log_message("WARN", "setting hostname '%s': %s", cfg.hostname, strerror(errno));
        } else {
            scout_log_message("INFO", "hostname set to '%s'", cfg.hostname);
        }
        if (scout_write_hostname_file("/etc/hostname", cfg.hostname) != 0) {
            scout_log_errno("WARN", "writing /etc/hostname");
        }
    }

    SCOUT_DBG("main: resolving interface %s", cfg.interface);
    if (scout_platform_get_interface(cfg.interface, &iface) != 0) {
        scout_log_errno("ERROR", "resolving interface");
        SCOUT_DBG("main: scout_platform_get_interface FAILED errno=%d (%s)", errno, strerror(errno));
        return 1;
    }
    SCOUT_DBG("main: interface resolved, name=%s", iface.name);

    SCOUT_DBG("main: bringing link up");
    if (scout_platform_set_link_up(&iface) != 0) {
        scout_log_errno("WARN", "bringing link up");
        SCOUT_DBG("main: set_link_up FAILED (non-fatal) errno=%d (%s)", errno, strerror(errno));
    } else {
        SCOUT_DBG("main: link up OK");
    }

    scout_install_signal_handlers();

    while (!g_stop) {
        char addr[32];
        char router[32];

        if (g_reload) {
            g_reload = 0;
            if (scout_config_load(config_path, &cfg) != 0) {
                scout_log_errno("WARN", "reloading configuration");
            }
        }

        if (scout_dhcp_acquire_lease(&cfg, &iface, NULL, 0, &current_lease) != 0) {
            attempts++;
            scout_log_errno("WARN", "acquiring DHCP lease");
            if (cfg.max_retries != 0 && attempts >= cfg.max_retries) {
                scout_log_message("ERROR", "giving up after %u DHCP attempts", attempts);
                return 1;
            }
            if (scout_sleep_interruptible(cfg.retry_interval, &g_stop) != 0) {
                break;
            }
            continue;
        }

        attempts = 0;
        scout_format_ipv4(current_lease.address, addr, sizeof(addr));
        scout_format_ipv4(current_lease.router, router, sizeof(router));
        scout_log_message("INFO", "lease acquired for %s: address=%s router=%s lease=%us",
                          iface.name, addr, router, current_lease.lease_time);

        if (scout_refresh_outputs(&cfg, &iface, &current_lease) != 0) {
            return 1;
        }

        if (oneshot) {
            return 0;
        }

        if (current_lease.renewal_time <= cfg.renew_margin) {
            current_lease.renewal_time = cfg.renew_margin + 1u;
        }
        if (scout_sleep_interruptible(current_lease.renewal_time - cfg.renew_margin, &g_stop) != 0) {
            break;
        }

        while (!g_stop) {
            scout_lease_t renewed;
            if (scout_dhcp_acquire_lease(&cfg, &iface, &current_lease, 1, &renewed) == 0) {
                current_lease = renewed;
                scout_log_message("INFO", "lease renewed for %s", iface.name);
                if (scout_refresh_outputs(&cfg, &iface, &current_lease) != 0) {
                    return 1;
                }
                break;
            }

            scout_log_errno("WARN", "renewing DHCP lease");
            if (scout_sleep_interruptible(cfg.retry_interval, &g_stop) != 0) {
                break;
            }

            if (current_lease.lease_time != 0 &&
                (uint32_t)(time(NULL) - current_lease.obtained_at) >= current_lease.lease_time) {
                scout_log_message("WARN", "lease expired; starting a fresh discovery");
                break;
            }
        }
    }

    scout_log_message("INFO", "shutting down");
    return 0;
}
