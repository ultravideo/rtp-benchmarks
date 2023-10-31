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

struct stream_results {
    size_t packets_received = 0;
    size_t bytes_received = 0;
    long long start = 0;
    long long last = 0;
};

struct hook_args {
    uvgrtp::media_stream* stream = nullptr;
    stream_results* res = nullptr;
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

    std::string result_filename           = argv[1];
    std::string local_address = argv[2];
    int local_port            = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port            = atoi(argv[5]);

    //int nthreads               = atoi(argv[6]);
    //bool vvc_enabled  = get_vvc_state(argv[7]);
    //bool atlas_enabled  = get_atlas_state(argv[7]);
    //bool srtp_enabled = get_srtp_state(argv[8]);

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

    streams.ad->install_receive_hook(&ad_a, hook);
    streams.ovd->install_receive_hook(&ovd_a, hook);
    streams.gvd->install_receive_hook(&gvd_a, hook);
    streams.avd->install_receive_hook(&avd_a, hook);
    
    while (frame_received)
    {
        frame_received = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT));
    }
    std::cout << "No more frames received for " << TIMEOUT << " ms, end round" << std::endl;

    sess->destroy_stream(streams.ad);
    sess->destroy_stream(streams.ovd);
    sess->destroy_stream(streams.gvd);
    sess->destroy_stream(streams.avd);
    rtp_ctx.destroy_session(sess);

    // Calculate results
    long long start = find_earliest_time_point(ad_r.start, ovd_r.start, gvd_r.start, avd_r.start);
    long long end   = find_latest_time_point(ad_r.last, ovd_r.last, gvd_r.last, avd_r.last);
    long long diff = end - start;

    size_t total_packets_received = ad_r.packets_received + ovd_r.packets_received + gvd_r.packets_received + avd_r.packets_received;
    size_t total_bytes_received = ad_r.bytes_received + ovd_r.bytes_received + gvd_r.bytes_received + avd_r.bytes_received;

    write_receive_results_to_file(result_filename, total_bytes_received, total_packets_received, diff);

    return EXIT_SUCCESS;
}

void hook(void* arg, uvgrtp::frame::rtp_frame* frame)
{
    hook_args* args = (hook_args*)arg;
    stream_results* results = args->res;

    if (results->packets_received == 0) {
        results->start = get_current_time();
    }

    if (!frame) {
        std::cerr << "Receiver test failed!" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        return;
    }

    results->last = get_current_time();
    results->bytes_received += frame->payload_len;
    results->packets_received++;
    (void)uvg_rtp::frame::dealloc_frame(frame);
    frame_received = true;
}
