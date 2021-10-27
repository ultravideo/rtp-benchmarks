#include "uvgrtp_util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <iostream>

struct thread_info {
    size_t pkts;
    size_t bytes;
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point last;
} *thread_info;

std::atomic<int> nready(0);

void hook(void* arg, uvg_rtp::frame::rtp_frame* frame);

void receiver_thread(char* addr, int thread_num, std::string local_address, int local_port,
    std::string remote_address, int remote_port, bool vvc, bool srtp);

int main(int argc, char** argv)
{
    if (argc != 8) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <number of threads> <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]) + 200;

    int nthreads = atoi(argv[5]);
    std::string format = argv[6];

    bool vvc = false;
    if (format == "vvc" || format == "h266")
    {
        vvc = true;
    }
    else if (format != "hevc" && format != "h265")
    {
        std::cerr << "Unsupported uvgRTP receiver format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp = false; // TODO

    thread_info = (struct thread_info*)calloc(nthreads, sizeof(*thread_info));

    for (int i = 0; i < nthreads; ++i)
        new std::thread(receiver_thread, argv[1], i, local_address, local_port, remote_address, remote_port, vvc, srtp);


    // TODO: use thread.join. Also delete threads
    while (nready.load() < nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return EXIT_SUCCESS;
}

void hook(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    int tid = *(int*)arg;

    if (thread_info[tid].pkts == 0)
        thread_info[tid].start = std::chrono::high_resolution_clock::now();

    /* receiver returns NULL to indicate that it has not received a frame in 10s
     * and the sender has likely stopped sending frames long time ago so the benchmark
     * can proceed to next run and ma*/
    if (!frame) {
        fprintf(stderr, "discard %zu %zu %lu\n", thread_info[tid].bytes, thread_info[tid].pkts,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                thread_info[tid].last - thread_info[tid].start
                ).count()
        );
        nready++;
        while (true)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    thread_info[tid].last = std::chrono::high_resolution_clock::now();
    thread_info[tid].bytes += frame->payload_len;

    (void)uvg_rtp::frame::dealloc_frame(frame);

    if (++thread_info[tid].pkts == EXPECTED_FRAMES) {
        fprintf(stderr, "%zu %zu %lu\n", thread_info[tid].bytes, thread_info[tid].pkts,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                thread_info[tid].last - thread_info[tid].start
                ).count()
        );
        nready++;
    }
}

void receiver_thread(char* addr, int thread_num, std::string local_address, int local_port, 
    std::string remote_address, int remote_port, bool vvc, bool srtp)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;
    uint16_t thread_local_port = local_port + thread_num * 2;
    uint16_t thread_remote_port = remote_port + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &receive, local_address, remote_address, thread_local_port, thread_remote_port, vvc, srtp, true);

    int tid = thread_num / 2;
    receive->install_receive_hook(&tid, hook);

    while (nready)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    cleanup_uvgrtp(rtp_ctx, session, receive);
}

