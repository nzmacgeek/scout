#include "config.h"

#include "common.h"
#include "scout_config.h"
#include "scout_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
#include "scout_blueyos_netctl.h"
#else
#if defined(HAVE_GETIFADDRS)
#include <ifaddrs.h>
#endif
#include <net/route.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#define DEFAULT_ROUTE_METRIC 0u
#define EXEC_FAILURE_EXIT_CODE 127
#define SCOUTD_PATH "/sbin/scoutd"
#define SCOUTD_FALLBACK "scoutd"

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage:\n"
            "  scoutctl iface show [IFACE]\n"
            "  scoutctl iface up IFACE\n"
            "  scoutctl iface down IFACE\n"
            "  scoutctl route show\n"
            "  scoutctl route add DEST[/PREFIX]|default GATEWAY IFACE [METRIC]\n"
            "  scoutctl route del DEST[/PREFIX]|default GATEWAY IFACE [METRIC]\n"
            "  scoutctl dhcp renew [-c FILE]\n"
            "  scoutctl dhcp release [-c FILE]\n"
            "  scoutctl dns flush\n"
            "  scoutctl --version\n");
}

static int parse_cidr(const char *text, uint32_t *addr_out, uint8_t *prefix_out)
{
    char tmp[64];
    char *slash;
    uint32_t addr;
    uint32_t prefix_len = 32;
    size_t text_len;

    if (!text || !addr_out || !prefix_out) {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(text, "default") == 0) {
        *addr_out = 0;
        *prefix_out = 0;
        return 0;
    }

    text_len = strlen(text);
    if (text_len > sizeof(tmp) - 1u) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(tmp, text, text_len + 1u);

    slash = strchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        if (scout_parse_u32(slash + 1, &prefix_len) != 0 || prefix_len > 32u) {
            errno = EINVAL;
            return -1;
        }
    }

    if (scout_parse_ipv4(tmp, &addr) != 0) {
        errno = EINVAL;
        return -1;
    }

    *addr_out = addr;
    *prefix_out = (uint8_t)prefix_len;
    return 0;
}

/* Forward declaration: defined after platform-specific block below. */
static int print_iface_details(const char *ifname);

static int read_text_file(const char *path, char *buf, size_t buf_size)
{
    FILE *fp;

    if (!path || !buf || buf_size == 0) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }

    if (!fgets(buf, (int)buf_size, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static void print_if_flags(uint32_t flags)
{
    printf("    flags: 0x%08x", flags);
    if (flags & IFF_UP) {
        printf(" UP");
    }
    if (flags & IFF_RUNNING) {
        printf(" RUNNING");
    }
    if (flags & IFF_BROADCAST) {
        printf(" BROADCAST");
    }
    if (flags & IFF_MULTICAST) {
        printf(" MULTICAST");
    }
    printf("\n");
}

static int route_show(void)
{
#if defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    fprintf(stderr, "scoutctl: route listing is not supported on this platform\n");
    return 1;
#else
    FILE *fp;
    char line[256];

    fp = fopen("/proc/net/route", "r");
    if (!fp) {
        scout_log_errno("ERROR", "opening /proc/net/route");
        return 1;
    }

    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        scout_log_errno("ERROR", "reading route header");
        return 1;
    }

    printf("Iface Destination Prefix Gateway Metric Flags\n");
    while (fgets(line, sizeof(line), fp)) {
        char ifname[32];
        unsigned long dst;
        unsigned long gw;
        unsigned long flags;
        unsigned long mask;
        int metric;
        char dst_text[32];
        char gw_text[32];

        /* /proc/net/route columns used: Iface Destination Gateway Flags Metric Mask. */
        if (sscanf(line, "%31s %lx %lx %lx %*s %*s %d %lx",
                   ifname, &dst, &gw, &flags, &metric, &mask) != 6) {
            continue;
        }

        scout_format_ipv4(ntohl((uint32_t)dst), dst_text, sizeof(dst_text));
        scout_format_ipv4(ntohl((uint32_t)gw), gw_text, sizeof(gw_text));
        printf("%s %s %u %s %d 0x%lx\n",
               ifname,
               dst_text,
               scout_netmask_to_prefix(ntohl((uint32_t)mask)),
               gw_text,
               metric,
               flags);
    }

    fclose(fp);
    return 0;
#endif
}

static int run_child(char *const argv[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(EXEC_FAILURE_EXIT_CODE);
    }

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    errno = EIO;
    return -1;
}

static int load_config(const char *path, scout_config_t *cfg)
{
    if (!cfg) {
        errno = EINVAL;
        return -1;
    }

    if (scout_config_load(path ? path : "/etc/scout/scout.conf", cfg) != 0) {
        scout_log_errno("ERROR", "loading configuration");
        return -1;
    }

    return 0;
}

static int command_exists(const char *path)
{
    return access(path, X_OK) == 0;
}

/* --- Platform-specific implementations --- */

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
static void copy_ifname(struct ifreq *ifr, const char *ifname)
{
    memset(ifr->ifr_name, 0, sizeof(ifr->ifr_name));
    strncpy(ifr->ifr_name, ifname, sizeof(ifr->ifr_name) - 1);
}

static void print_iface_addresses(const char *ifname)
{
#if defined(HAVE_GETIFADDRS)
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) != 0) {
        return;
    }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        char addr[INET6_ADDRSTRLEN];
        void *src = NULL;

        if (!ifa->ifa_name || strcmp(ifa->ifa_name, ifname) != 0 || !ifa->ifa_addr) {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {
            src = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            src = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
        } else {
            continue;
        }

        if (!inet_ntop(ifa->ifa_addr->sa_family, src, addr, sizeof(addr))) {
            continue;
        }

        printf("    addr: %s\n", addr);
        found = 1;
    }

    freeifaddrs(ifaddr);

    if (!found) {
        printf("    addr: (none)\n");
    }
#else
    (void)ifname;
    printf("    addr: unavailable on this build\n");
#endif
}

static int route_set_sockaddr(struct sockaddr_in *sin, uint32_t addr)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(addr);
    return 0;
}

static int route_apply(const char *op, uint32_t dst, uint8_t prefix_len, uint32_t gateway,
                       const char *ifname, uint32_t metric)
{
    int fd;
    struct rtentry route;
    struct sockaddr_in sin;
    int rc;

    if (!op || !ifname) {
        errno = EINVAL;
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&route, 0, sizeof(route));
    route_set_sockaddr(&sin, dst);
    memcpy(&route.rt_dst, &sin, sizeof(sin));
    route_set_sockaddr(&sin, scout_prefix_to_netmask(prefix_len));
    memcpy(&route.rt_genmask, &sin, sizeof(sin));
    route_set_sockaddr(&sin, gateway);
    memcpy(&route.rt_gateway, &sin, sizeof(sin));
    route.rt_flags = RTF_UP;
    if (gateway != 0) {
        route.rt_flags |= RTF_GATEWAY;
    }
    /* metric is parsed as uint32_t, so only the upper signed-short bound is relevant. */
    if (metric > (uint32_t)SHRT_MAX) {
        close(fd);
        errno = ERANGE;
        return -1;
    }
    route.rt_metric = (short)metric;
    route.rt_dev = (char *)ifname;

    if (strcmp(op, "add") == 0) {
        rc = ioctl(fd, SIOCADDRT, &route);
    } else {
        rc = ioctl(fd, SIOCDELRT, &route);
    }

    close(fd);
    return rc;
}

static int route_command(int argc, char **argv)
{
    uint32_t dst;
    uint8_t prefix;
    uint32_t gateway;
    uint32_t metric = DEFAULT_ROUTE_METRIC;

    if (argc < 1) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[0], "show") == 0) {
        return route_show();
    }

    if ((strcmp(argv[0], "add") == 0 || strcmp(argv[0], "del") == 0) && argc >= 4) {
        if (parse_cidr(argv[1], &dst, &prefix) != 0 ||
            scout_parse_ipv4(argv[2], &gateway) != 0) {
            fprintf(stderr, "scoutctl: invalid route destination or gateway\n");
            return 2;
        }

        if (argc >= 5 && scout_parse_u32(argv[4], &metric) != 0) {
            fprintf(stderr, "scoutctl: invalid metric\n");
            return 2;
        }

        if (route_apply(argv[0], dst, prefix, gateway, argv[3], metric) != 0) {
            scout_log_errno("ERROR", strcmp(argv[0], "add") == 0 ? "adding route" : "deleting route");
            return 1;
        }
        return 0;
    }

    usage(stderr);
    return 2;
}

static int dhcp_release(const scout_config_t *cfg)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_in sin;

    if (unlink(cfg->lease_file) != 0 && errno != ENOENT) {
        scout_log_errno("WARN", "removing lease file");
    }

    if (scout_write_text_file_atomic(cfg->resolv_conf, "# Generated by scout\n") != 0) {
        scout_log_errno("WARN", "resetting resolv.conf");
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        scout_log_errno("WARN", "opening interface socket for release");
        return 1;
    }

    memset(&ifr, 0, sizeof(ifr));
    copy_ifname(&ifr, cfg->interface);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    memcpy(&ifr.ifr_addr, &sin, sizeof(sin));
    if (ioctl(fd, SIOCSIFADDR, &ifr) != 0) {
        scout_log_errno("WARN", "clearing interface address");
    }

#ifdef SIOCSIFNETMASK
    memcpy(&ifr.ifr_netmask, &sin, sizeof(sin));
    if (ioctl(fd, SIOCSIFNETMASK, &ifr) != 0) {
        scout_log_errno("WARN", "clearing interface netmask");
    }
#endif

    close(fd);
    return 0;
}

static int dns_flush(void)
{
    char *const resolvectl_argv[] = { "/usr/bin/resolvectl", "flush-caches", NULL };
    char *const systemd_resolve_argv[] = { "/usr/bin/systemd-resolve", "--flush-caches", NULL };
    char *const nscd_argv[] = { "/usr/sbin/nscd", "-i", "hosts", NULL };

    if (command_exists(resolvectl_argv[0])) {
        return run_child(resolvectl_argv);
    }
    if (command_exists(systemd_resolve_argv[0])) {
        return run_child(systemd_resolve_argv);
    }
    if (command_exists(nscd_argv[0])) {
        return run_child(nscd_argv);
    }

    printf("No known DNS cache daemon command found; nothing to flush.\n");
    return 0;
}

#else  /* SCOUT_ENABLE_BLUEYOS_NETCTL */

static void print_iface_addresses(const char *ifname)
{
    (void)ifname;
    printf("    addr: (not available via netctl)\n");
}

static int route_command(int argc, char **argv)
{
    uint32_t dst;
    uint8_t prefix;
    uint32_t gateway;
    uint32_t metric = DEFAULT_ROUTE_METRIC;
    scout_iface_t iface;

    if (argc < 1) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[0], "show") == 0) {
        return route_show();
    }

    if (strcmp(argv[0], "add") == 0 && argc >= 4) {
        if (parse_cidr(argv[1], &dst, &prefix) != 0 ||
            scout_parse_ipv4(argv[2], &gateway) != 0) {
            fprintf(stderr, "scoutctl: invalid route destination or gateway\n");
            return 2;
        }

        if (argc >= 5 && scout_parse_u32(argv[4], &metric) != 0) {
            fprintf(stderr, "scoutctl: invalid metric\n");
            return 2;
        }

        if (scout_platform_get_interface(argv[3], &iface) != 0) {
            fprintf(stderr, "scoutctl: unknown interface '%s'\n", argv[3]);
            return 1;
        }

        if (scout_blueyos_netctl_route_add(iface.ifindex, dst, prefix, gateway, metric) != 0) {
            scout_log_errno("ERROR", "adding route");
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[0], "del") == 0) {
        fprintf(stderr, "scoutctl: route deletion is not supported on this platform\n");
        return 2;
    }

    usage(stderr);
    return 2;
}

static int dhcp_release(const scout_config_t *cfg)
{
    if (unlink(cfg->lease_file) != 0 && errno != ENOENT) {
        scout_log_errno("WARN", "removing lease file");
    }

    if (scout_write_text_file_atomic(cfg->resolv_conf, "# Generated by scout\n") != 0) {
        scout_log_errno("WARN", "resetting resolv.conf");
    }

    fprintf(stderr,
            "scoutctl: lease files cleared, but the live IP address and default route\n"
            "          remain assigned (addr/route removal not yet supported via netctl).\n");
    return 1;
}

static int dns_flush(void)
{
    printf("No DNS cache daemon available on this platform.\n");
    return 0;
}

#endif  /* SCOUT_ENABLE_BLUEYOS_NETCTL */

/* --- Common implementations (use platform-specific helpers above) --- */

static int print_iface_details(const char *ifname)
{
    scout_iface_t iface;
    char mac[32];
    char path[256];
    char value[64];

    if (scout_platform_get_interface(ifname, &iface) != 0) {
        return -1;
    }

    scout_dump_mac(iface.mac, mac, sizeof(mac));

    printf("%s\n", iface.name);
    printf("    index: %u\n", iface.ifindex);
    printf("    mac: %s\n", mac);
    printf("    mtu: %u\n", iface.mtu);
    print_if_flags(iface.flags);

    if (snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface.name) < (int)sizeof(path) &&
        read_text_file(path, value, sizeof(value)) == 0) {
        printf("    operstate: %s\n", value);
    }

    if (snprintf(path, sizeof(path), "/sys/class/net/%s/speed", iface.name) < (int)sizeof(path) &&
        read_text_file(path, value, sizeof(value)) == 0) {
        printf("    speed: %s Mb/s\n", value);
    }

    if (snprintf(path, sizeof(path), "/sys/class/net/%s/type", iface.name) < (int)sizeof(path) &&
        read_text_file(path, value, sizeof(value)) == 0) {
        printf("    link_type: %s\n", value);
    }

    print_iface_addresses(iface.name);
    return 0;
}

static int iface_list_callback(const scout_iface_t *iface, void *userdata)
{
    (void)userdata;
    if (print_iface_details(iface->name) != 0) {
        scout_log_errno("WARN", "reading interface");
    }
    return 0;
}

static int iface_show(const char *ifname)
{
    if (ifname) {
        if (print_iface_details(ifname) != 0) {
            scout_log_errno("ERROR", "reading interface");
            return 1;
        }
        return 0;
    }

    if (scout_platform_list_interfaces(iface_list_callback, NULL) != 0) {
        scout_log_errno("ERROR", "listing interfaces");
        return 1;
    }
    return 0;
}

static int iface_set(const char *ifname, int up)
{
    scout_iface_t iface;

    if (scout_platform_get_interface(ifname, &iface) != 0) {
        scout_log_errno("ERROR", "reading interface");
        return 1;
    }

    if (up) {
        if (scout_platform_set_link_up(&iface) != 0) {
            scout_log_errno("ERROR", "setting interface up");
            return 1;
        }
    } else {
        if (scout_platform_set_link_down(&iface) != 0) {
            scout_log_errno("ERROR", "setting interface down");
            return 1;
        }
    }

    return 0;
}

static int dhcp_command(int argc, char **argv)
{
    const char *cfg_path = NULL;
    scout_config_t cfg;

    if (argc < 1) {
        usage(stderr);
        return 2;
    }

    if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        cfg_path = argv[2];
    }

    if (strcmp(argv[0], "renew") == 0) {
        char *renew_argv[6];
        int idx = 0;

        renew_argv[idx++] = command_exists(SCOUTD_PATH) ? SCOUTD_PATH : SCOUTD_FALLBACK;
        renew_argv[idx++] = "-1";
        if (cfg_path) {
            renew_argv[idx++] = "-c";
            renew_argv[idx++] = (char *)cfg_path;
        }
        renew_argv[idx] = NULL;

        return run_child(renew_argv);
    }

    if (strcmp(argv[0], "release") == 0) {
        if (load_config(cfg_path, &cfg) != 0) {
            return 1;
        }
        return dhcp_release(&cfg);
    }

    usage(stderr);
    return 2;
}

static int iface_command(int argc, char **argv)
{
    if (argc < 1) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[0], "show") == 0) {
        return iface_show(argc >= 2 ? argv[1] : NULL);
    }

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[0], "up") == 0) {
        return iface_set(argv[1], 1);
    }

    if (strcmp(argv[0], "down") == 0) {
        return iface_set(argv[1], 0);
    }

    usage(stderr);
    return 2;
}

int main(int argc, char **argv)
{
    scout_set_program_name("scoutctl");

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("scoutctl %s\n", PACKAGE_VERSION);
        return 0;
    }

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    if (strcmp(argv[1], "iface") == 0) {
        return iface_command(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "route") == 0) {
        return route_command(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "dhcp") == 0) {
        return dhcp_command(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "dns") == 0 && argc == 3 && strcmp(argv[2], "flush") == 0) {
        return dns_flush();
    }

    usage(stderr);
    return 2;
}
