/* 
 * compile: gcc udperf.c
 * start server: ./a.out -s
 * start client: ./a.out -a 127.0.0.1
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef PORT
#   define PORT          8888
#endif
#ifndef PKT_LEN
#   define PKT_LEN       1458
#endif
#ifndef MAX_ROUNDS
#   define MAX_ROUNDS      10
#endif
#ifndef MAX_PKTS
#   define MAX_PKTS    350000
#endif

static inline float diff_ms(struct timespec s, struct timespec e)
{
    return ((float)((e.tv_sec - s.tv_sec) * (long)1e9 + e.tv_nsec - s.tv_nsec)) / 1000 / 1000;
}

static int usage(void)
{
    fprintf(stderr, "server: ./%s -s\n", __FILE__);
    fprintf(stderr, "client: ./%s -a <ip of server>\n", __FILE__);

    return -1;
}

static int server(void)
{
    int s_u, s_t, s_n;
    struct sockaddr_in sa_u, sa_t;

    /* initialize server udp socket */
    memset(&sa_u, 0, sizeof(sa_u));

    sa_u.sin_family      = AF_INET;
    sa_u.sin_port        = htons(PORT);
    sa_u.sin_addr.s_addr = htonl(INADDR_ANY);

    s_u = socket(AF_INET, SOCK_DGRAM, 0);

    (void)bind(s_u, (struct sockaddr *)&sa_u, sizeof(sa_u));

    /* initialize server tcp socket */
    memset(&sa_t, 0, sizeof(sa_t));

    sa_t.sin_family      = AF_INET;
    sa_t.sin_port        = htons(PORT + 1);
    sa_t.sin_addr.s_addr = htonl(INADDR_ANY);

    s_t = socket(AF_INET, SOCK_STREAM, 0);
    (void)setsockopt(s_t, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));
    (void)bind(s_t, (struct sockaddr *)&sa_t, sizeof(sa_t));

    (void)listen(s_t, 1);
    s_n = accept(s_t, (struct sockaddr *)&sa_t, &(socklen_t){ sizeof(sa_t) });

    /* receive packets from remote and once select() timeouts,
     * send how many packets were received */
    uint8_t buffer[PKT_LEN];

    fd_set read_fs;
    FD_ZERO(&read_fs);

    for (int i = 0, npkts = 0; i < MAX_ROUNDS; ++i, npkts = 0) {
        while (npkts != MAX_PKTS) {
            FD_SET(s_u, &read_fs);

            if (!select(s_u + 1, &read_fs, NULL, NULL, &(struct timeval){ 2, 0 }))
                break;

            while (recv(s_u, buffer, PKT_LEN, MSG_DONTWAIT) > 0)
                ++npkts;
        }
        (void)send(s_n, &npkts, sizeof(int), 0);
    }

    return 0;
}

static int client(char *server_addr)
{
    float total = 0;
    struct timespec start, end;
    uint8_t data[PKT_LEN] = { 0 };
    struct sockaddr_in sa_t, sa_u;
    int s_u, s_t, runtime, pkts, bytes;

    if (!server_addr) 
        return usage();

    /* initialize client udp socket */
    memset(&sa_u, 0, sizeof(sa_u));

    sa_u.sin_family = AF_INET;
    sa_u.sin_port   = htons(PORT);

    (void)inet_pton(AF_INET, server_addr, &sa_u.sin_addr);
    s_u = socket(AF_INET, SOCK_DGRAM, 0);

    /* initialize client tcp socket */
    memset(&sa_t, 0, sizeof(sa_t));

    sa_t.sin_family = AF_INET;
    sa_t.sin_port   = htons(PORT + 1);

    (void)inet_pton(AF_INET, server_addr, &sa_t.sin_addr);
    s_t = socket(AF_INET, SOCK_STREAM, 0);

    (void)connect(s_t, (struct sockaddr *)&sa_t, sizeof(sa_t));

    for (int i = 0; i < MAX_ROUNDS; ++i) {

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int k = 0; k < MAX_PKTS; ++k)
            (void)sendto(s_u, data, PKT_LEN, 0, (struct sockaddr *)&sa_u, sizeof(sa_u));

        clock_gettime(CLOCK_MONOTONIC, &end);

        runtime = diff_ms(start, end);
        bytes   = MAX_PKTS * PKT_LEN;

        /* read how many packets the server received */
        (void)recv(s_t, &pkts, sizeof(int), 0);

        fprintf(stderr, "%.2lf Gb/s, %.2lf Mb/s, %d MB transferred, %.2f%% packets received\n",
            (float)bytes * 8 / 1000 / 1000 / 1000 / runtime * 1000,
            (float)bytes * 8 / 1000 / 1000 / runtime * 1000,
            bytes / 1000 / 1000,
            ((float)pkts / MAX_PKTS) * 100
        );

        total += (float)bytes * 8 / 1000 / 1000 / 1000 / runtime * 1000;
    }

    fprintf(stderr, "\naverage goodput: %.2lf Gb/s\n", (float)total / MAX_ROUNDS);

    return 0;
}

int main(int argc, char **argv)
{
    char *cvalue = NULL;
    int srv      = 0;
    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "sa:")) != -1) {
        switch (c) {
            case 'a':
                cvalue = optarg;
                break;

            case 's':
                srv = 1;
                break;

            default:
                return usage();
        }
    }

    return srv ? server() : client(cvalue);
}
