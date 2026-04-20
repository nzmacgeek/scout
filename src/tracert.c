#include "config.h"

#include "common.h"
#include "scout_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static void usage(FILE *stream)
{
    fprintf(stream, "Usage: tracert HOST\n");
}

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
static int tracert_linux(const char *host)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct sockaddr_in dst;
    int udp_fd;
    int icmp_fd;
    int ttl;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) {
        fprintf(stderr, "tracert: unable to resolve %s\n", host);
        freeaddrinfo(res);
        return 1;
    }

    memcpy(&dst, res->ai_addr, sizeof(dst));
    freeaddrinfo(res);

    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (udp_fd < 0 || icmp_fd < 0) {
        fprintf(stderr, "tracert: %s\n", strerror(errno));
        if (udp_fd >= 0) close(udp_fd);
        if (icmp_fd >= 0) close(icmp_fd);
        return 1;
    }

    printf("Tracing route to %s\n", host);
    for (ttl = 1; ttl <= 30; ++ttl) {
        uint8_t probe = 0;
        fd_set rfds;
        struct timeval tv;
        int port = 33434 + ttl;
        int reached = 0;

        dst.sin_port = htons((unsigned short)port);
        if (setsockopt(udp_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0) {
            fprintf(stderr, "tracert: setsockopt(IP_TTL): %s\n", strerror(errno));
            close(udp_fd);
            close(icmp_fd);
            return 1;
        }

        if (sendto(udp_fd, &probe, sizeof(probe), 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            fprintf(stderr, "tracert: sendto: %s\n", strerror(errno));
            close(udp_fd);
            close(icmp_fd);
            return 1;
        }

        FD_ZERO(&rfds);
        FD_SET(icmp_fd, &rfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        printf("%2d  ", ttl);
        if (select(icmp_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            uint8_t recvbuf[1024];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            ssize_t nread = recvfrom(icmp_fd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&from, &from_len);
            if (nread > (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
                struct iphdr *ip = (struct iphdr *)recvbuf;
                struct icmphdr *icmp = (struct icmphdr *)(recvbuf + ip->ihl * 4u);
                char addr[32];
                scout_format_ipv4(ntohl(from.sin_addr.s_addr), addr, sizeof(addr));
                printf("%s\n", addr);
                if (icmp->type == ICMP_DEST_UNREACH && from.sin_addr.s_addr == dst.sin_addr.s_addr) {
                    reached = 1;
                }
            } else {
                printf("*\n");
            }
        } else {
            printf("*\n");
        }

        if (reached) {
            break;
        }
    }

    close(udp_fd);
    close(icmp_fd);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    scout_set_program_name("tracert");

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("tracert %s\n", PACKAGE_VERSION);
        return 0;
    }

    if (argc != 2) {
        usage(stderr);
        return 2;
    }

    if (!scout_platform_raw_diag_supported()) {
        fprintf(stderr, "tracert: %s\n", scout_platform_raw_diag_reason());
        return 2;
    }

#if !defined(SCOUT_ENABLE_BLUEYOS_NETCTL)
    return tracert_linux(argv[1]);
#else
    return 2;
#endif
}
