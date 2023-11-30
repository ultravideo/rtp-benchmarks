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

bool srtp_enabled = false;

struct stream_results { // Save stats of each stream
    size_t packets_received = 0;
    size_t bytes_received = 0;
    long long start = 0;
    long long last = 0;
};

bool frame_received = true;
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
    srtp_enabled          = get_srtp_state(argv[8]);

    std::cout << "Starting uvgRTP V-PCC receiver tests. " << local_address << ":" << local_port 
        << "<-" << remote_address << ":" << remote_port << std::endl;

    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = 0;
    if (srtp_enabled) {
        flags = RCE_SRTP | RCE_SRTP_KMNGMNT_USER;
    }
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, true);

    if (srtp_enabled) {
        std::cout << "SRTP enabled" << std::endl;
        uint8_t key[KEY_SIZE] = { 0 };
        uint8_t salt[SALT_SIZE] = { 0 };

        for (int i = 0; i < KEY_SIZE; ++i)
            key[i] = i + 7;
        for (int i = 0; i < SALT_SIZE; ++i)
            key[i] = i + 13;

        streams.ad->add_srtp_ctx(key, salt);
        streams.ovd->add_srtp_ctx(key, salt);
        streams.gvd->add_srtp_ctx(key, salt);
        streams.avd->add_srtp_ctx(key, salt);
    }

    stream_results ad_r;
    stream_results ovd_r;
    stream_results gvd_r;
    stream_results avd_r;

    streams.ad->install_receive_hook(&ad_r, hook);
    streams.ovd->install_receive_hook(&ovd_r, hook);
    streams.gvd->install_receive_hook(&gvd_r, hook);
    streams.avd->install_receive_hook(&avd_r, hook);
    
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
    stream_results* results = (stream_results*)arg;

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
