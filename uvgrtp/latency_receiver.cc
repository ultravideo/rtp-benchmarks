#include "uvgrtp_util.h"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>


size_t nframes = 0;

void hook_receiver(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    // send the frame immediately back
    uvg_rtp::media_stream receive = (uvg_rtp::media_stream *)arg;
    receive->push_frame(frame->payload, frame->payload_len, 0);
    nframes++;
}

int receiver(void)
{
    std::string addr("127.0.0.1");

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* receive = nullptr;
    uint16_t send_port = SENDER_PORT;
    uint16_t receive_port = RECEIVER_PORT;

    intialize_uvgrtp(rtp_ctx, &session, &receive, addr_, addr_, receive_port, send_port, false);

    receive->install_receive_hook(receive, hook_receiver);

    while (nframes < EXPECTED_FRAMES)
        std::this_thread::sleep_for(std::chrono::milliseconds(3));

    cleanup_uvgrtp(rtp_ctx, session, receive);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    (void)argc, (void)argv;

    return receiver();
}
