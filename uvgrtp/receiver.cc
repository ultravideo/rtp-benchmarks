#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>
#include <cstring>
#include <algorithm>

#define KEY_SIZE   16
#define SALT_SIZE  14

//#define USE_SRTP

#define USE_VVC

struct thread_info {
    size_t pkts;
    size_t bytes;
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point last;
} *thread_info;

std::atomic<int> nready(0);

constexpr int LOCAL_PORT = 8880;
constexpr int REMOTE_PORT = 9880;

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
        while (1)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    thread_info[tid].last = std::chrono::high_resolution_clock::now();
    thread_info[tid].bytes += frame->payload_len;

    (void)uvg_rtp::frame::dealloc_frame(frame);

    if (++thread_info[tid].pkts == 602) {
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
    uvg_rtp::context rtp_ctx;
#ifdef USE_SRTP
    std::cout << "Start running uvgRTP SRTP benchmark, receiver end" << std::endl;
    int flags = RCE_SRTP | RCE_SRTP_KMNGMNT_USER;
#else // USE_SRTP
    std::cout << "Start running uvgRTP RTP benchmark, receiver end" << std::endl;
    int flags = 0;
#endif


#ifdef USE_VVC
    rtp_format_t fmt = RTP_FORMAT_H266;
#else
    rtp_format_t fmt = RTP_FORMAT_H265;
#endif

    auto sess = rtp_ctx.create_session(addr_);
    auto hevc = sess->create_stream(
        LOCAL_PORT + thread_num*2,
        REMOTE_PORT + thread_num*"",
        fmt,
        flags
    );
#ifdef USE_SRTP
    uint8_t key[KEY_SIZE] = { 0 };
    uint8_t salt[SALT_SIZE] = { 0 };

    for (int i = 0; i < KEY_SIZE; ++i)
        key[i] = i + 7;
    for (int i = 0; i < SALT_SIZE; ++i)
        key[i] = i + 13;

    hevc->add_srtp_ctx(key, salt);

#endif

    int tid = thread_num / 2;
    hevc->install_receive_hook(&tid, hook);

    for (;;)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

    rtp_ctx.destroy_session(sess);
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: ./%s <remote address> <number of threads>\n", __FILE__);
        return -1;
    }

    int nthreads = atoi(argv[2]);
    thread_info = (struct thread_info*)calloc(nthreads, sizeof(*thread_info));

    for (int i = 0; i < nthreads; ++i)
        new std::thread(thread_func, argv[1], i * 2);

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
