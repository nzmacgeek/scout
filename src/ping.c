#include "config.h"

#include "common.h"
#include "scout_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

static void usage(FILE *stream)
{
    fprintf(stream, "Usage: ping [-c COUNT] HOST\n");
}

static int ping_icmp(const char *host, unsigned int count)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct sockaddr_in dst;
    int fd;
    unsigned short ident = (unsigned short)getpid();
    unsigned int seq;
    unsigned int received = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_ICMP;

    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        fprintf(stderr, "ping: unable to resolve %s\n", host);
        freeaddrinfo(res);
        return 1;
    }

    memcpy(&dst, res->ai_addr, sizeof(dst));
    freeaddrinfo(res);

    fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        fprintf(stderr, "ping: %s\n", strerror(errno));
        return 1;
    }

    printf("PING %s\n", host);
    for (seq = 0; seq < count; ++seq) {
        uint8_t packet[64];
        struct icmphdr *icmp = (struct icmphdr *)packet;
        fd_set rfds;
        struct timeval tv;
        ssize_t nread;

        memset(packet, 0, sizeof(packet));
        icmp->type = ICMP_ECHO;
        icmp->code = 0;
        icmp->un.echo.id = htons(ident);
        icmp->un.echo.sequence = htons((unsigned short)(seq + 1u));
        memset(packet + sizeof(*icmp), 0xa5, sizeof(packet) - sizeof(*icmp));
        icmp->checksum = scout_checksum16(packet, sizeof(packet));

        if (sendto(fd, packet, sizeof(packet), 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            fprintf(stderr, "ping: sendto: %s\n", strerror(errno));
            close(fd);
            return 1;
        }

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            uint8_t recvbuf[512];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            nread = recvfrom(fd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&from, &from_len);
            if (nread > (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
                struct iphdr *ip = (struct iphdr *)recvbuf;
                struct icmphdr *reply = (struct icmphdr *)(recvbuf + ip->ihl * 4u);
                if (reply->type == ICMP_ECHOREPLY && ntohs(reply->un.echo.id) == ident) {
                    char addr[32];
                    scout_format_ipv4(ntohl(from.sin_addr.s_addr), addr, sizeof(addr));
                    printf("%zu bytes from %s: icmp_seq=%u\n",
                           sizeof(packet) - sizeof(*icmp), addr, seq + 1u);
                    received++;
                }
            }
        } else {
            printf("Request timeout for icmp_seq %u\n", seq + 1u);
        }
    }

    close(fd);
    printf("--- %s ping statistics ---\n", host);
    printf("%u packets transmitted, %u packets received\n", count, received);
    return received ? 0 : 1;
}

int main(int argc, char **argv)
{
    unsigned int count = 4;
    int opt;

    scout_set_program_name("ping");

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ping %s\n", PACKAGE_VERSION);
        return 0;
    }

    while ((opt = getopt(argc, argv, "c:h")) != -1) {
        switch (opt) {
        case 'c':
            count = (unsigned int)strtoul(optarg, NULL, 10);
            break;
        case 'h':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 2;
        }
    }

    if (optind >= argc) {
        usage(stderr);
        return 2;
    }

    if (!scout_platform_raw_diag_supported()) {
        fprintf(stderr, "ping: %s\n", scout_platform_raw_diag_reason());
        return 2;
    }

    return ping_icmp(argv[optind], count);
}
