#include "uvgrtp_util.h"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>


struct thread_info {
    size_t pkts;
    size_t bytes;
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point last;
} *thread_info;

std::atomic<int> nready(0);

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

void thread_func(char* addr, int thread_num)
{
    std::string addr_("127.0.0.1");

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;
    uint16_t send_port = SENDER_PORT + thread_num * 2;
    uint16_t receive_port = RECEIVER_PORT + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &receive, addr_, addr_, receive_port, send_port, false, false);

    int tid = thread_num / 2;
    receive->install_receive_hook(&tid, hook);

    for (;;)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    cleanup_uvgrtp(rtp_ctx, session, receive);
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: ./%s <remote address> <number of threads>\n", __FILE__);
        return EXIT_FAILURE;
    }

    int nthreads = atoi(argv[2]);
    thread_info = (struct thread_info*)calloc(nthreads, sizeof(*thread_info));

    for (int i = 0; i < nthreads; ++i)
        new std::thread(thread_func, argv[1], i * 2);

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return EXIT_SUCCESS;
}
