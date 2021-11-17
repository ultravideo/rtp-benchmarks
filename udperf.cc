/* 
 * compile: g++ udperf.cpp
 * start server: ./a.out -s 127.0.0.1
 * start client: ./a.out -c 127.0.0.1
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
#include <string>

#include <iostream>

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

static int server(std::string address, uint16_t port, int packet_size)
{
    int s_u = 0;
    int s_t = 0;
    int s_n = 0;
    struct sockaddr_in sa_u;
    struct sockaddr_in sa_t;
    
    /* initialize server udp socket */
    memset(&sa_u, 0, sizeof(sa_u));

    sa_u.sin_family      = AF_INET;
    sa_u.sin_port        = htons(port);
    
    if (address == "0.0.0.0")
    {
        sa_u.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        if (!inet_pton(AF_INET, address.c_str(), &sa_u.sin_addr.s_addr))
        {
            std::cerr << "Failed to set server UDP address" << std::endl;
            return EXIT_FAILURE;
        }
    }

    s_u = socket(AF_INET, SOCK_DGRAM, 0);

    (void)bind(s_u, (struct sockaddr *)&sa_u, sizeof(sa_u));

    /* initialize server tcp socket */
    memset(&sa_t, 0, sizeof(sa_t));

    sa_t.sin_family      = AF_INET;
    sa_t.sin_port        = htons(port + 1);
    
    sa_t.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (address == "0.0.0.0")
    {
        sa_t.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        if (!inet_pton(AF_INET, address.c_str(), &sa_t.sin_addr.s_addr))
        {
            std::cerr << "Failed to set server TCP address" << std::endl;
            return EXIT_FAILURE;
        }
    }

    s_t = socket(AF_INET, SOCK_STREAM, 0);
    int opt_value = 1;
    (void)setsockopt(s_t, SOL_SOCKET, SO_REUSEADDR, &opt_value, sizeof(int));
    (void)bind(s_t, (struct sockaddr *)&sa_t, sizeof(sa_t));

    (void)listen(s_t, 1);
    socklen_t sa_size = sizeof(sa_t);
    s_n = accept(s_t, (struct sockaddr *)&sa_t, &sa_size);

    /* receive packets from remote and once select() timeouts,
     * send how many packets were received */
    uint8_t buffer[packet_size];

    fd_set read_fs;
    FD_ZERO(&read_fs);

    for (int i = 0, npkts = 0; i < MAX_ROUNDS; ++i, npkts = 0) {
        while (npkts != MAX_PKTS) {
            FD_SET(s_u, &read_fs);
            timeval tim = {2, 0}; // 2 seconds
            if (!select(s_u + 1, &read_fs, NULL, NULL, &tim))
                break;

            while (recv(s_u, buffer, packet_size, MSG_DONTWAIT) > 0)
                ++npkts;
        }
        (void)send(s_n, &npkts, sizeof(int), 0);
    }

    return EXIT_SUCCESS;
}

static int client(std::string address, uint16_t port, int packet_size)
{
    float total = 0;
    struct timespec start, end;
    uint8_t data[packet_size] = { 0 };
    struct sockaddr_in sa_t, sa_u;
    int s_u, s_t, runtime, pkts, bytes;

    /* initialize client udp socket */
    memset(&sa_u, 0, sizeof(sa_u));

    sa_u.sin_family = AF_INET;
    sa_u.sin_port   = htons(port);

    if (!inet_pton(AF_INET, address.c_str(), &sa_u.sin_addr))
    {
        std::cerr << "Failed to set client UDP address" << std::endl;
        return EXIT_FAILURE;
    }
    s_u = socket(AF_INET, SOCK_DGRAM, 0);

    /* initialize client tcp socket */
    memset(&sa_t, 0, sizeof(sa_t));

    sa_t.sin_family = AF_INET;
    sa_t.sin_port   = htons(port + 1);

    if (!inet_pton(AF_INET, address.c_str(), &sa_t.sin_addr))
    {
        std::cerr << "Failed to set client TCP address" << std::endl;
        return EXIT_FAILURE;
    }

    s_t = socket(AF_INET, SOCK_STREAM, 0);

    (void)connect(s_t, (struct sockaddr *)&sa_t, sizeof(sa_t));

    for (int i = 0; i < MAX_ROUNDS; ++i) {

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int k = 0; k < MAX_PKTS; ++k)
            (void)sendto(s_u, data, packet_size, 0, (struct sockaddr *)&sa_u, sizeof(sa_u));

        clock_gettime(CLOCK_MONOTONIC, &end);

        runtime = diff_ms(start, end);
        bytes   = MAX_PKTS * packet_size;

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

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int c = 0;
    opterr = 0;
    
    std::string address = "";
    uint16_t port = 0;
    bool run_client = false;
    bool run_server = false;
    int packet_size = 0;
    
    int rvalue = EXIT_FAILURE;
    while ((c = getopt(argc, argv, "c:s:p:i:")) != -1) {
        switch (c) {
            case 'c':
            {
                address = optarg;
                run_client = true;
                break;
            }
            case 's':
            {
                address = optarg;
                run_server = true;
                break;
            }
            case 'p':
            {
                port = atoi(optarg);
                break;
            }
            case 'i':
            {
                packet_size = atoi(optarg);
                break;
            }
            default:
                return rvalue;
        }
    }
    
    if (run_server)
    {
        rvalue = server(address, port, packet_size);
    }
    
    if (run_client)
    {
        if (address == "")
        {
            std::cerr << "Please provide address!" << std::endl;
            return EXIT_FAILURE;
        }
        rvalue = client(address, port, packet_size);
    }

    if (!run_server && !run_client)
    {
        std::cerr << "Please specify which end to run!" << std::endl;
        return EXIT_FAILURE;
    }
    
    return rvalue;
}
