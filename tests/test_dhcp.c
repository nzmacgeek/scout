#include "config.h"

#include "scout_dhcp.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t options[] = {
        53, 1, 5,
        1, 4, 255, 255, 255, 0,
        3, 4, 192, 168, 1, 1,
        6, 8, 8, 8, 8, 8, 1, 1, 1, 1,
        51, 4, 0, 0, 14, 16,
        54, 4, 192, 168, 1, 254,
        255
    };
    scout_lease_t lease;
    uint8_t msg_type = 0;

    memset(&lease, 0, sizeof(lease));
    assert(scout_dhcp_parse_options(options, sizeof(options), &lease, &msg_type) == 0);
    assert(msg_type == 5);
    assert(lease.subnet_mask == 0xffffff00u);
    assert(lease.router == 0xc0a80101u);
    assert(lease.server_id == 0xc0a801feu);
    assert(lease.dns_count == 2);
    assert(lease.dns[0] == 0x08080808u);
    assert(lease.dns[1] == 0x01010101u);
    assert(lease.lease_time == 3600u);

    return 0;
}
