#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <chrono>

bool frame_received = true;
int TIMEOUT = 300;
bool srtp_enabled = false;

int ad = 0;
int ovd = 0;
int gvd = 0;
int avd = 0;

// encryption parameters
enum Key_length{SRTP_128 = 128, SRTP_196 = 196, SRTP_256 = 256};
constexpr Key_length KEY_S = SRTP_256;
constexpr int KEY_SIZE_BYTES = KEY_S/8;
constexpr int SALT_S = 112;
constexpr int SALT_SIZE_BYTES = SALT_S/8;

void ad_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;
    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    ad++;
}
void ovd_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;
    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    ovd++;
}
void gvd_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;
    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    gvd++;
}
void avd_hook_rec(void* arg, uvg_rtp::frame::rtp_frame* frame)
{
    uvgrtp::media_stream* receive = (uvgrtp::media_stream*)arg;
    if((receive->push_frame(frame->payload, frame->payload_len, RTP_NO_H26X_SCL)) != RTP_OK) {
        std::cout << "Error sending frame" << std::endl;
    }
    frame_received = true;
    avd++;
}

int receiver(std::string local_address, int local_port, std::string remote_address, int remote_port)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = 0;
    if (srtp_enabled) {
        flags = RCE_SRTP | RCE_SRTP_KMNGMNT_USER | RCE_SRTP_KEYSIZE_256;
    }
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, true);

    if (srtp_enabled) {
        std::cout << "SRTP enabled" << std::endl;
        uint8_t key[KEY_SIZE_BYTES]   = { 0 };
        uint8_t salt[SALT_SIZE_BYTES] = { 0 };

        // initialize SRTP key and salt with dummy values
        for (int i = 0; i < KEY_SIZE_BYTES; ++i)
            key[i] = i;

        for (int i = 0; i < SALT_SIZE_BYTES; ++i)
            salt[i] = i * 2;

        streams.ad->add_srtp_ctx(key, salt);
        streams.ovd->add_srtp_ctx(key, salt);
        streams.gvd->add_srtp_ctx(key, salt);
        streams.avd->add_srtp_ctx(key, salt);
    }

    streams.ad->install_receive_hook(streams.ad, ad_hook_rec);
    streams.ovd->install_receive_hook(streams.ovd, ovd_hook_rec);
    streams.gvd->install_receive_hook(streams.gvd, gvd_hook_rec);
    streams.avd->install_receive_hook(streams.avd, avd_hook_rec);
    
    while (frame_received)
    {
        frame_received = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT));
    }
    std::cout << "No more frames received for " << TIMEOUT << " ms, end round" << std::endl;

    // Debug prints
    std::cout << "AD NALS received: " << ad << std::endl;
    std::cout << "OVD NALS received: " << ovd << std::endl;
    std::cout << "GVD NALS received: " << gvd << std::endl;
    std::cout << "AVD NALS received: " << avd << std::endl;

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
    //bool vvc_enabled = get_vvc_state(argv[5]);
    //bool atlas_enabled = get_atlas_state(argv[5]);
    srtp_enabled = get_srtp_state(argv[6]);

    return receiver(local_address, local_port, remote_address, remote_port);
}
