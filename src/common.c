#include "config.h"

#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *g_program_name = "scout";

void scout_set_program_name(const char *name)
{
    if (name && *name) {
        g_program_name = name;
    }
}

const char *scout_program_name(void)
{
    return g_program_name;
}

void scout_log_message(const char *level, const char *fmt, ...)
{
    va_list ap;
    time_t now;
    struct tm tm_now;
    char ts[32];

    now = time(NULL);
    memset(&tm_now, 0, sizeof(tm_now));
    localtime_r(&now, &tm_now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(stderr, "%s [%s] %s: ", ts, level, scout_program_name());
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void scout_log_errno(const char *level, const char *context)
{
    scout_log_message(level, "%s: %s", context, strerror(errno));
}

int scout_parse_u32(const char *text, uint32_t *value_out)
{
    char *end = NULL;
    unsigned long value;

    if (!text || !*text || !value_out) {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > UINT_MAX) {
        return -1;
    }

    *value_out = (uint32_t)value;
    return 0;
}

int scout_parse_ipv4(const char *text, uint32_t *addr_out)
{
    struct in_addr addr;

    if (!text || !addr_out) {
        return -1;
    }

    if (inet_pton(AF_INET, text, &addr) != 1) {
        return -1;
    }

    *addr_out = ntohl(addr.s_addr);
    return 0;
}

int scout_parse_bool(const char *text, int *value_out)
{
    if (!text || !value_out) {
        return -1;
    }

    if (strcmp(text, "1") == 0 || strcasecmp(text, "yes") == 0 ||
        strcasecmp(text, "true") == 0 || strcasecmp(text, "on") == 0) {
        *value_out = 1;
        return 0;
    }

    if (strcmp(text, "0") == 0 || strcasecmp(text, "no") == 0 ||
        strcasecmp(text, "false") == 0 || strcasecmp(text, "off") == 0) {
        *value_out = 0;
        return 0;
    }

    return -1;
}

void scout_format_ipv4(uint32_t addr, char *buf, size_t buf_size)
{
    struct in_addr in;

    if (!buf || buf_size == 0) {
        return;
    }

    in.s_addr = htonl(addr);
    if (!inet_ntop(AF_INET, &in, buf, buf_size)) {
        snprintf(buf, buf_size, "0.0.0.0");
    }
}

uint32_t scout_prefix_to_netmask(unsigned int prefix_len)
{
    if (prefix_len == 0) {
        return 0;
    }
    if (prefix_len >= 32) {
        return 0xffffffffu;
    }
    return 0xffffffffu << (32u - prefix_len);
}

unsigned int scout_netmask_to_prefix(uint32_t mask)
{
    unsigned int prefix = 0;

    while (mask & 0x80000000u) {
        prefix++;
        mask <<= 1u;
    }
    return prefix;
}

uint16_t scout_checksum16(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    size_t i;

    for (i = 0; i + 1 < len; i += 2) {
        sum += (uint32_t)((bytes[i] << 8) | bytes[i + 1]);
    }

    if (i < len) {
        sum += (uint32_t)(bytes[i] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

char *scout_trim(char *text)
{
    char *end;

    if (!text) {
        return NULL;
    }

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }

    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

int scout_mkdir_p(const char *path, unsigned int mode)
{
    char tmp[PATH_MAX];
    size_t i;

    if (!path || !*path) {
        return -1;
    }

    if (strlen(path) >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (i = 1; tmp[i] != '\0'; ++i) {
        if (tmp[i] != '/') {
            continue;
        }

        tmp[i] = '\0';
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
            return -1;
        }
        tmp[i] = '/';
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int scout_parent_dir(const char *path, char *dir_out, size_t dir_out_size)
{
    const char *slash;
    size_t len;

    if (!path || !dir_out || dir_out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        snprintf(dir_out, dir_out_size, ".");
        return 0;
    }

    len = (size_t)(slash - path);
    if (len == 0) {
        len = 1;
    }
    if (len >= dir_out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(dir_out, path, len);
    dir_out[len] = '\0';
    return 0;
}

int scout_write_text_file_atomic(const char *path, const char *contents)
{
    char dir_path[PATH_MAX];
    char tmp_template[PATH_MAX];
    int fd;
    FILE *fp;

    if (!path || !contents) {
        errno = EINVAL;
        return -1;
    }

    if (scout_parent_dir(path, dir_path, sizeof(dir_path)) != 0) {
        return -1;
    }
    if (scout_mkdir_p(dir_path, 0755) != 0) {
        return -1;
    }

    if (snprintf(tmp_template, sizeof(tmp_template), "%s/.scout.tmp.XXXXXX", dir_path) >= (int)sizeof(tmp_template)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fd = mkstemp(tmp_template);
    if (fd < 0) {
        return -1;
    }

    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(tmp_template);
        return -1;
    }

    if (fputs(contents, fp) == EOF || fflush(fp) != 0 || fsync(fd) != 0 || fclose(fp) != 0) {
        unlink(tmp_template);
        return -1;
    }

    if (rename(tmp_template, path) != 0) {
        unlink(tmp_template);
        return -1;
    }

    return 0;
}

int scout_sleep_interruptible(unsigned int seconds, volatile sig_atomic_t *stop_flag)
{
    while (seconds > 0) {
        if (stop_flag && *stop_flag) {
            return -1;
        }
        sleep(1);
        seconds--;
    }
    return 0;
}

void scout_dump_mac(const uint8_t mac[6], char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return;
    }

    snprintf(buf, buf_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
