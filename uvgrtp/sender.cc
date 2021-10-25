#include "uvgrtp_util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>

extern void* get_mem(int argc, char** argv, size_t& len);

std::atomic<int> nready(0);

void thread_func(void* mem, size_t len, char* addr, int thread_num, double fps)
{
    std::string addr_("127.0.0.1");

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;
    uint16_t send_port = SENDER_PORT + thread_num * 2;
    uint16_t receive_port = RECEIVER_PORT + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &send, addr_, addr_, send_port, receive_port, false, false);

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
            memcpy(&chunk_size, (uint8_t *)mem + offset, sizeof(uint64_t));

            offset     += sizeof(uint64_t);
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
    diff     = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    fprintf(stderr, "%lu bytes, %lu kB, %lu MB took %lu ms %lu s\n",
        bytes_sent, bytes_sent / 1000, bytes_sent / 1000 / 1000,
        diff, diff / 1000
    );

    cleanup_uvgrtp(rtp_ctx, session, send);

end:
    nready++;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: ./%s <remote address> <number of threads> <fps> \n", __FILE__);
        return EXIT_FAILURE;
    }

    size_t len   = 0;
    void *mem    = get_mem(0, NULL, len);
    int nthreads = atoi(argv[2]);
    std::thread **threads = (std::thread **)malloc(sizeof(std::thread *) * nthreads);

    for (int i = 0; i < nthreads; ++i)
        threads[i] = new std::thread(thread_func, mem, len, argv[1], i * 2, atof(argv[3]));

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (int i = 0; i < nthreads; ++i) {
        threads[i]->join();
        delete threads[i];
    }

    free(threads);

    return EXIT_SUCCESS;
}
