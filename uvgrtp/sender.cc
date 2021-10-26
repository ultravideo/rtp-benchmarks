#include "uvgrtp_util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <iostream>

extern void* get_mem(std::string filename, size_t& len);

void sender_thread(void* mem, size_t len, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp);

std::atomic<int> nready(0);


int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]);

    int nThreads = atoi(argv[5]);
    int fps = atoi(argv[6]);
    std::string format = argv[7];

    bool vvc = false;

    if (format == "vvc" || format == "h266")
    {
        vvc = true;
    }
    else if (format != "hevc" && format != "h265")
    {
        std::cerr << "Unsupported uvgRTP sender format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp = false; // TODO

    size_t len   = 0;
    void *mem    = get_mem("test_file.hevc", len);
    int nthreads = atoi(argv[2]);
    std::thread **threads = (std::thread **)malloc(sizeof(std::thread *) * nthreads);

    for (int i = 0; i < nthreads; ++i)
        threads[i] = new std::thread(sender_thread, mem, len, local_address, local_port, remote_address, remote_port, i, fps, vvc, srtp);

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (int i = 0; i < nthreads; ++i) {
        threads[i]->join();
        delete threads[i];
    }

    free(threads);

    return EXIT_SUCCESS;
}

void sender_thread(void* mem, size_t len, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;
    uint16_t thread_local_port = local_port + thread_num * 2;
    uint16_t thread_remote_port = remote_port + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &send, local_address, remote_address, thread_local_port, thread_remote_port, vvc, srtp, false);

    size_t bytes_sent = 0;
    uint64_t chunk_size = 0;
    uint64_t total_size = 0;
    uint64_t diff = 0;
    uint64_t current = 0;
    uint64_t period = (uint64_t)((1000 / (float)fps) * 1000);
    rtp_error_t ret = RTP_OK;

    // start the sending test
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (int rounds = 0; rounds < 1; ++rounds) {
        for (size_t offset = 0, k = 0; offset < len; ++k) {
            memcpy(&chunk_size, (uint8_t*)mem + offset, sizeof(uint64_t));

            offset += sizeof(uint64_t);
            total_size += chunk_size;

            if ((ret = send->push_frame((uint8_t*)mem + offset, chunk_size, 0)) != RTP_OK) {
                fprintf(stderr, "push_frame() failed!\n");
                for (;;);
            }

            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start
                ).count();

            offset += chunk_size;
            bytes_sent += chunk_size;

            if (runtime < current * period)
                std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));

            current += 1;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    fprintf(stderr, "%lu bytes, %lu kB, %lu MB took %lu ms %lu s\n",
        bytes_sent, bytes_sent / 1000, bytes_sent / 1000 / 1000,
        diff, diff / 1000
    );

    cleanup_uvgrtp(rtp_ctx, session, send);

end:
    nready++;
}