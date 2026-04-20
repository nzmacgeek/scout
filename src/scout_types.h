#ifndef SCOUT_TYPES_H
#define SCOUT_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SCOUT_MAX_DNS_SERVERS 3

typedef struct {
    char name[32];
    unsigned int ifindex;
    uint8_t mac[6];
    uint32_t flags;
    uint32_t mtu;
    int carrier;
} scout_iface_t;

typedef struct {
    uint32_t address;
    uint32_t subnet_mask;
    uint32_t router;
    uint32_t server_id;
    uint32_t lease_time;
    uint32_t renewal_time;
    uint32_t rebind_time;
    uint32_t dns[SCOUT_MAX_DNS_SERVERS];
    size_t dns_count;
    char domain[256];
    time_t obtained_at;
} scout_lease_t;

#endif
