#include "config.h"

#include "common.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage: nslookup HOST [HOST...]\n"
            "       nslookup --version\n");
}

int main(int argc, char **argv)
{
    int i;

    scout_set_program_name("nslookup");

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("nslookup %s\n", PACKAGE_VERSION);
        return 0;
    }

    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    for (i = 1; i < argc; ++i) {
        struct addrinfo hints;
        struct addrinfo *res = NULL;
        struct addrinfo *ai;
        int rc;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        rc = getaddrinfo(argv[i], NULL, &hints, &res);
        if (rc != 0) {
            fprintf(stderr, "%s: %s\n", argv[i], gai_strerror(rc));
            continue;
        }

        printf("%s\n", argv[i]);
        for (ai = res; ai; ai = ai->ai_next) {
            char host[NI_MAXHOST];
            if (getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof(host), NULL, 0, NI_NUMERICHOST) == 0) {
                printf("  %s\n", host);
            }
        }
        freeaddrinfo(res);
    }

    return 0;
}
