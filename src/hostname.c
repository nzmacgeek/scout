#include "config.h"

#include "common.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

static void usage(FILE *stream)
{
    fprintf(stream, "Usage: hostname [NAME]\n");
}

int main(int argc, char **argv)
{
    char name[HOST_NAME_MAX + 1];

    scout_set_program_name("hostname");

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("hostname %s\n", PACKAGE_VERSION);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }

    if (argc == 1) {
        if (gethostname(name, sizeof(name)) != 0) {
            fprintf(stderr, "hostname: %s\n", strerror(errno));
            return 1;
        }
        name[sizeof(name) - 1] = '\0';
        printf("%s\n", name);
        return 0;
    }

    if (argc == 2) {
        size_t len = strlen(argv[1]);

        if (len == 0) {
            fprintf(stderr, "hostname: name must not be empty\n");
            return 2;
        }
        if (len > HOST_NAME_MAX) {
            fprintf(stderr, "hostname: name too long (max %d characters)\n", HOST_NAME_MAX);
            return 2;
        }
        if (sethostname(argv[1], len) != 0) {
            fprintf(stderr, "hostname: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    usage(stderr);
    return 2;
}
