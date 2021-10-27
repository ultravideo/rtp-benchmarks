#include "uvgrtp_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <iostream>

void sender_thread(void* mem, size_t len, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp, const std::string result_file);

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "usage: ./%s <input file> <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file = argv[1];
    std::string result_file = argv[2];

    std::string local_address = argv[3];
    int local_port = atoi(argv[4]) + 200;
    std::string remote_address = argv[5];
    int remote_port = atoi(argv[6]);

    std::cout << "Starting uvgRTP sender tests. " << local_address << ":" << local_port
        << "->" << remote_address << ":" << remote_port << std::endl;

    int nthreads = atoi(argv[7]);
    int fps = atoi(argv[8]);
    std::string format = argv[9];
    std::string srtp = argv[10];

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

    bool srtp_enabled = false;

    if (srtp == "1" || srtp == "yes" || srtp == "y")
    {
        srtp_enabled = true;
    }

    size_t len   = 0;
    void *mem    = get_mem(input_file, len);
    std::thread **threads = (std::thread **)malloc(sizeof(std::thread *) * nthreads);

    for (int i = 0; i < nthreads; ++i) {
        threads[i] = new std::thread(sender_thread, mem, len, local_address, local_port, remote_address, remote_port,
            i, fps, vvc, srtp_enabled, result_file);
    }

    for (int i = 0; i < nthreads; ++i) {
        if (threads[i]->joinable())
        {
            threads[i]->join();
        }
        delete threads[i];
        threads[i] = nullptr;
    }

    free(threads);

    return EXIT_SUCCESS;
}

void sender_thread(void* mem, size_t len, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp, const std::string result_file)
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

    auto end = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    write_send_results_to_file(result_file, bytes_sent, diff);
    cleanup_uvgrtp(rtp_ctx, session, send);
}