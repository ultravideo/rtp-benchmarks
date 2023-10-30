#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>

int TIMEOUT = 1000;
struct hook_args {
    uvgrtp::media_stream* stream = nullptr;
    stream_results* res = nullptr;
};

struct stream_results {
    size_t packets_received = 0;
    size_t bytes_received = 0;
    std::chrono::high_resolution_clock::time_point start = 0;
    std::chrono::high_resolution_clock::time_point last = 0;
};
bool frame_received = true;
int gvd_nals = 0;
int avd_nals = 0;

void hook(void* arg, uvg_rtp::frame::rtp_frame* frame);

int main(int argc, char** argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    result_filename           = argv[1];
    std::string local_address = argv[2];
    int local_port            = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port            = atoi(argv[5]);

    int nthreads               = atoi(argv[6]);
    bool vvc_enabled  = get_vvc_state(argv[7]);
    bool atlas_enabled  = get_atlas_state(argv[7]);
    bool srtp_enabled = get_srtp_state(argv[8]);

    std::cout << "Starting uvgRTP V-PCC receiver tests. " << local_address << ":" << local_port 
        << "<-" << remote_address << ":" << remote_port << std::endl;

    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = 0;
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, true);

    stream_results ad_r;
    stream_results ovd_r;
    stream_results gvd_r;
    stream_results avd_r;

    hook_args ad_a  = {streams.ad, &ad_r};
    hook_args ovd_a = {streams.ovd, &ovd_r};
    hook_args gvd_a = {streams.gvd, &gvd_r};
    hook_args avd_a = {streams.avd, &avd_r};

    streams.ad->install_receive_hook(&ad_a, ad_hook_rec);
    streams.ovd->install_receive_hook(&ovd_a, hook_receiver);
    streams.gvd->install_receive_hook(&gvd_a, gvd_hook_rec);
    streams.avd->install_receive_hook(&avd_a, avd_hook_rec);
    
    while (frame_received)
    {
        frame_received = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT));
    }
    std::cout << "No more frames received for " << TIMEOUT << " ms, end benchmark" << std::endl;

    sess->destroy_stream(streams.ad);
    sess->destroy_stream(streams.ovd);
    sess->destroy_stream(streams.gvd);
    sess->destroy_stream(streams.avd);
    rtp_ctx.destroy_session(sess);

    // TODO: Calculate bitrates etc

    return EXIT_SUCCESS;
}

void hook(void* arg, uvgrtp::frame::rtp_frame* frame)
{
    hook_args* args = (hook_args*)arg;
    stream_results* results = args->res;

    if (results.packets_received == 0) {
        results.start = std::chrono::high_resolution_clock::now();
    }


    if (!frame) {
        std::cerr << "Receiver test failed!" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        return;
    }

    results.last = std::chrono::high_resolution_clock::now();
    results.bytes_received += frame->payload_len;
    results.packets_received++;
    (void)uvg_rtp::frame::dealloc_frame(frame);
    frame_received = true;
}
