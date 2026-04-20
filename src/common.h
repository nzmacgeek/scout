#ifndef SCOUT_COMMON_H
#define SCOUT_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

#include "scout_types.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void scout_set_program_name(const char *name);
const char *scout_program_name(void);

void scout_log_message(const char *level, const char *fmt, ...);
void scout_log_errno(const char *level, const char *context);

int scout_parse_u32(const char *text, uint32_t *value_out);
int scout_parse_ipv4(const char *text, uint32_t *addr_out);
int scout_parse_bool(const char *text, int *value_out);
void scout_format_ipv4(uint32_t addr, char *buf, size_t buf_size);
uint32_t scout_prefix_to_netmask(unsigned int prefix_len);
unsigned int scout_netmask_to_prefix(uint32_t mask);
uint16_t scout_checksum16(const void *data, size_t len);
char *scout_trim(char *text);
int scout_mkdir_p(const char *path, unsigned int mode);
int scout_write_text_file_atomic(const char *path, const char *contents);
int scout_sleep_interruptible(unsigned int seconds, volatile sig_atomic_t *stop_flag);
void scout_dump_mac(const uint8_t mac[6], char *buf, size_t buf_size);

#endif
