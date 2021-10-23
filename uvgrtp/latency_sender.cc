#include "uvgrtp_util.h"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>

constexpr float LATENCY_TEST_FPS = 30.0f;

extern void *get_mem(int argc, char **argv, size_t& len);

std::chrono::high_resolution_clock::time_point start2;

size_t frames   = 0;
size_t ninters  = 0;
size_t nintras  = 0;

size_t total       = 0;
size_t total_intra = 0;
size_t total_inter = 0;

static void hook_sender(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg, (void)frame;

    if (frame) {

        uint64_t diff = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start2
        ).count();

        switch ((frame->payload[0] >> 1) & 0x3f) {
            case 19:
                total += (diff / 1000);
                total_intra += (diff / 1000);
                nintras++, frames++;
                break;

            case 1:
                total += (diff / 1000);
                total_inter += (diff / 1000);
                ninters++, frames++;
                break;
        }
    }
}

static int sender(void)
{
    std::string addr("127.0.0.1");

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;
    uint16_t send_port = SENDER_PORT + thread_num * 2;
    uint16_t receive_port = RECEIVER_PORT + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, session, send, addr_, addr_, send_port, receive_port, false, false);

    send->install_receive_hook(nullptr, hook_sender);

    size_t len = 0;
    void* mem = get_mem(0, NULL, len);
    uint64_t csize = 0;
    uint64_t diff = 0;
    uint64_t current = 0;
    uint64_t chunk_size = 0;
    uint64_t period = (uint64_t)((1000 / LATENCY_TEST_FPS) * 1000);
    rtp_error_t ret = RTP_OK;

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (int rounds = 0; rounds < 1; ++rounds) {
        for (size_t offset = 0, k = 0; offset < len; ++k) {
            memcpy(&chunk_size, (uint8_t *)mem + offset, sizeof(uint64_t));

            offset += sizeof(uint64_t);

            // record send time
            start2 = std::chrono::high_resolution_clock::now();
            if ((ret = send->push_frame((uint8_t *)mem + offset, chunk_size, 0)) != RTP_OK) {
                fprintf(stderr, "push_frame() failed!\n");
                cleanup_uvgrtp(rtp_ctx, session, send);
                return EXIT_FAILURE;
            }

            // wait until is the time to send next latency test frame
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start
            ).count();

            if (runtime < current * period)
                std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));

            current += 1;
            offset  += chunk_size;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just so we don't exit too soon

    cleanup_uvgrtp(rtp_ctx, session, send);

    fprintf(stderr, "%zu: intra %lf, inter %lf, avg %lf\n",
        frames,
        total_intra / (float)nintras,
        total_inter / (float)ninters,
        total / (float)frames
    );

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    (void)argc, (void)argv;

    return sender();
}
