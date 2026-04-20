#include "config.h"

#include "scout_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "test-config.conf";
    FILE *fp = fopen(path, "w");
    scout_config_t cfg;

    assert(fp != NULL);
    fputs("interface=wan0\nrequest_timeout=9\npersist_interfaces=no\n", fp);
    fclose(fp);

    assert(scout_config_load(path, &cfg) == 0);
    assert(cfg.request_timeout == 9);
    assert(cfg.persist_interfaces == 0);
    assert(strcmp(cfg.interface, "wan0") == 0);

    remove(path);
    return 0;
}
