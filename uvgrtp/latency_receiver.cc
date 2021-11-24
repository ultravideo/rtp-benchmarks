#include "uvgrtp_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <chrono>
#include <mutex>

std::chrono::high_resolution_clock::time_point last_packet_arrival;
std::mutex time_point_mutex;

void hook_receiver(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream *)arg;
    receive->push_frame(frame->payload, frame->payload_len, 0);
    time_point_mutex.lock();
    last_packet_arrival = std::chrono::high_resolution_clock::now();
    time_point_mutex.unlock();
}

int receiver(std::string local_address, int local_port, std::string remote_address, int remote_port,
    bool vvc_enabled, bool srtp_enabled)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;

    intialize_uvgrtp(rtp_ctx, &session, &receive, remote_address, local_address,
        local_port, remote_port, srtp_enabled, vvc_enabled);

    // the receiving end is not measured in latency tests
    time_point_mutex.lock();
    last_packet_arrival = std::chrono::high_resolution_clock::now();
    receive->install_receive_hook(receive, hook_receiver);
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - 
        last_packet_arrival).count() < 200)
    {
        time_point_mutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        time_point_mutex.lock();
    }
    time_point_mutex.unlock();

    cleanup_uvgrtp(rtp_ctx, session, receive);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]);
    bool vvc_enabled = get_vvc_state(argv[5]);
    bool srtp_enabled = get_srtp_state(argv[6]);

    return receiver(local_address, local_port, remote_address, remote_port, vvc_enabled, srtp_enabled);
}
