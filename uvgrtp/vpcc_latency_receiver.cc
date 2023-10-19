#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <chrono>

bool frame_received = true;
int gvd_nals = 0;
int avd_nals = 0;

void hook_receiver(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;

    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;

}
void ad_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;

    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    //std::cout << "AD nal unit" << std::endl;
}
void gvd_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;

    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    //std::cout << "GVD nal unit, size " << frame->payload_len << std::endl;
    gvd_nals++;
}

void avd_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    // send the frame immediately back
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;

    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    //std::cout << "AVD nal unit, size " << frame->payload_len << std::endl;
    avd_nals++;
}

int receiver(std::string local_address, int local_port, std::string remote_address, int remote_port)
{
    int timout = 250;
    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = 0;
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, true);

    streams.ad->install_receive_hook(streams.ad, ad_hook_rec);
    streams.ovd->install_receive_hook(streams.ovd, hook_receiver);
    streams.gvd->install_receive_hook(streams.gvd, gvd_hook_rec);
    streams.avd->install_receive_hook(streams.avd, avd_hook_rec);
    
    while (frame_received)
    {
        frame_received = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(timout));
    }
    std::cout << "No more frames received for " << timout << " ms, end benchmark" << std::endl;
    //std::cout << "gvd nals " << gvd_nals << std::endl;
    //std::cout << "avd nals " << avd_nals << std::endl;

    sess->destroy_stream(streams.ad);
    sess->destroy_stream(streams.ovd);
    sess->destroy_stream(streams.gvd);
    sess->destroy_stream(streams.avd);
    rtp_ctx.destroy_session(sess);

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
    bool atlas_enabled = get_atlas_state(argv[5]);
    bool srtp_enabled = get_srtp_state(argv[6]);

    (void*)vvc_enabled;
    (void*)atlas_enabled;
    (void*)srtp_enabled;

    return receiver(local_address, local_port, remote_address, remote_port);
}
