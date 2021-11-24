#include "uvgrtp_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <deque>
#include <chrono>

std::mutex time_mutex;
std::deque<std::chrono::high_resolution_clock::time_point> frame_send_times;

size_t frames   = 0;
size_t ninters  = 0;
size_t nintras  = 0;

size_t total       = 0;
size_t total_intra = 0;
size_t total_inter = 0;

bool vvc_headers = false;

uint64_t get_diff()
{
    time_mutex.lock();
    uint64_t diff = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - frame_send_times.front()
        ).count();
    frame_send_times.pop_front();
    time_mutex.unlock();

    return diff;
}

static void hook_sender(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;

    if (frame) {

        uint64_t diff = 0;
        if (vvc_headers)
        {
            switch (frame->payload[2] & 0x3f) {
                case 19: // intra frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_intra += (diff / 1000);
                    nintras++;
                    frames++;
                    break;
                case 1: // inter frame
                {
                    diff = get_diff();
                    total += (diff / 1000);
                    total_inter += (diff / 1000);
                    ninters++;
                    frames++;
                    break;
                }
            }
        }
        else
        {
            switch ((frame->payload[0] >> 1) & 0x3f) {
                case 19: // intra frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_intra += (diff / 1000);
                    nintras++;
                    frames++;
                    break;
                case 1: // inter frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_inter += (diff / 1000);
                    ninters++;
                    frames++;
                    break;
            }
        }
    }
}

static int sender(std::string input_file, std::string local_address, int local_port, 
    std::string remote_address, int remote_port, float fps, bool vvc_enabled, bool srtp_enabled)
{
    vvc_headers = vvc_enabled;

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;

    intialize_uvgrtp(rtp_ctx, &session, &send, remote_address, local_address,
        local_port, remote_port, srtp_enabled, vvc_enabled);

    send->install_receive_hook(nullptr, hook_sender);

    size_t len = 0;
    void* mem = get_mem(input_file, len);

    std::vector<uint64_t> chunk_sizes;
    get_chunk_sizes(get_chunk_filename(input_file), chunk_sizes);

    if (mem == nullptr || chunk_sizes.empty())
    {
        return EXIT_FAILURE;
    }

    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 * 1000 / fps) );
    size_t offset = 0;
    rtp_error_t ret = RTP_OK;

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (auto& chunk_size : chunk_sizes)
    {

        // record send time
        time_mutex.lock();
        frame_send_times.push_back(std::chrono::high_resolution_clock::now());
        time_mutex.unlock();
        if ((ret = send->push_frame((uint8_t*)mem + offset, chunk_size, 0)) != RTP_OK) {
            fprintf(stderr, "push_frame() failed!\n");
            cleanup_uvgrtp(rtp_ctx, session, send);
            return EXIT_FAILURE;
        }

        current_frame += 1;
        offset += chunk_size;

        // wait until is the time to send next latency test frame
        auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();

        if (runtime < current_frame * period)
            std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
    }

    // just so we don't exit before last frame has arrived. Does not affect results
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 

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
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <input file> <local address> <local port> <remote address> <remote port> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file     = argv[1];

    std::string local_address  = argv[2];
    int local_port             = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port            = atoi(argv[5]);

    float fps                  = atof(argv[6]);
    bool vvc_enabled           = get_vvc_state(argv[7]);
    bool srtp_enabled          = get_srtp_state(argv[8]);

    return sender(input_file, local_address, local_port, remote_address, remote_port, fps, vvc_enabled, srtp_enabled);
}