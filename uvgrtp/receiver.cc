#include "uvgrtp_util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>

struct thread_info {
    size_t pkts;
    size_t bytes;
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point last;
} *thread_info;

std::atomic<int> nready(0);
std::atomic<int> frames_received(0);

void hook(void* arg, uvg_rtp::frame::rtp_frame* frame);

void receiver_thread(char* addr, int thread_num, int nthreads, std::string local_address, int local_port,
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
    int remote_port = atoi(argv[4]);

    std::cout << "Starting uvgRTP receiver tests. " << local_address << ":" << local_port 
        << "<-" << remote_address << ":" << remote_port << std::endl;

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

    std::vector<std::thread*> threads = {};

    for (int i = 0; i < nthreads; ++i) {
        threads.push_back(new std::thread(receiver_thread, argv[1], i, nthreads, local_address, local_port,
            remote_address, remote_port, vvc, srtp));
    }

    // wait all the thread executions to end and delete them
    for (int i = 0; i < nthreads; ++i) {
        if (threads[i]->joinable())
        {
            threads[i]->join();
        }
        delete threads[i];
        threads[i] = nullptr;
    }

    return EXIT_SUCCESS;
}

void receiver_thread(char* addr, int thread_num, int nthreads, std::string local_address, int local_port,
    std::string remote_address, int remote_port, bool vvc, bool srtp)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;
    uint16_t thread_local_port = local_port + thread_num * 2;
    uint16_t thread_remote_port = remote_port + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &receive, remote_address, local_address,
        thread_local_port, thread_remote_port, vvc, srtp);

    int tid = thread_num / 2;
    size_t previous_packets = 0;
    if (receive->install_receive_hook(&tid, hook) == RTP_OK)
    {
        //std::cout << "Installed hook to port: " << thread_local_port << std::endl;

        while (nready.load() < nthreads) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            if (frames_received.load() == previous_packets)
            {
                std::cerr << "uvgRTP receiver timed out. No packets received for 2 s. Received " 
                    << thread_info[tid].pkts << " frames in total" << std::endl;
                break;
            }

            previous_packets = frames_received.load();
        }
    }
    else
    {
        std::cerr << "Failed to install receive hook. Aborting test" << std::endl;
    }

    cleanup_uvgrtp(rtp_ctx, session, receive);
}

void hook(void* arg, uvgrtp::frame::rtp_frame* frame)
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

        while (true) {
            std::cerr << "Receiver test failed!" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    thread_info[tid].last = std::chrono::high_resolution_clock::now();
    thread_info[tid].bytes += frame->payload_len;

    (void)uvg_rtp::frame::dealloc_frame(frame);
    ++thread_info[tid].pkts;
    ++frames_received; // so we detect a possible timeout

    if (thread_info[tid].pkts == EXPECTED_FRAMES) {
        fprintf(stderr, "%zu %zu %lu\n", thread_info[tid].bytes, thread_info[tid].pkts,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                thread_info[tid].last - thread_info[tid].start
                ).count()
        );
        nready++;
    }
}