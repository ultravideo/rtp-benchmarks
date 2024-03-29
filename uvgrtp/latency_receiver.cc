#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <chrono>

bool frame_received = true;
int total_frames_received = 0;
bool atlas_enabled = false;

size_t ninters  = 0;
size_t nintras  = 0;
size_t n_non_vcl = 0;

void hook_receiver(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;
    int flags = 0;
    if(atlas_enabled) {
        flags = RTP_NO_H26X_SCL;
    }
    if((receive->push_frame(frame->payload, frame->payload_len, flags)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    ++total_frames_received;

    uint8_t nalu_t = (frame->payload[4] >> 1) & 0x3f;
        if (nalu_t <= 15) { // inter frame
            ninters++;
        }
        else if (nalu_t >= 16 && nalu_t <= 23) { // intra frame
            nintras++;
        }
        else {
            n_non_vcl++;
        }
}

int receiver(std::string local_address, int local_port, std::string remote_address, int remote_port,
    bool vvc_enabled, bool srtp_enabled, bool atlas)
{
    int timout = 250;
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;

    intialize_uvgrtp(rtp_ctx, &session, &receive, remote_address, local_address,
        local_port, remote_port, srtp_enabled, vvc_enabled, true, atlas);

    // the receiving end is not measured in latency tests
    receive->install_receive_hook(receive, hook_receiver);
    
    while (frame_received && total_frames_received < EXPECTED_FRAMES)
    {
        frame_received = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(timout));
    }

    if (total_frames_received < EXPECTED_FRAMES)
    {
        std::cout << "Received " << total_frames_received << " frames. No more frames received for "
            << timout << " ms." << std::endl;
    }
    std::cout << "intras: " << nintras << ", inters: " << ninters << ", non-vcl: " << n_non_vcl << std::endl;
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
    atlas_enabled = get_atlas_state(argv[5]);
    bool srtp_enabled = get_srtp_state(argv[6]);

    return receiver(local_address, local_port, remote_address, remote_port, vvc_enabled, srtp_enabled, atlas_enabled);
}
