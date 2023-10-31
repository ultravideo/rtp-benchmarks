#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <chrono>
#include <vector>

std::vector<long long> ad_send = {};
std::vector<long long> ovd_send = {};
std::vector<long long> gvd_send = {};
std::vector<long long> avd_send = {};
    

std::vector<long long> ad_recv = {};
std::vector<long long> ovd_recv = {};
std::vector<long long> gvd_recv = {};
std::vector<long long> avd_recv = {};

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, int fmt, float fps, const std::vector<v3c_unit_info> &units,
    std::vector<long long> &send_times);

static void ad_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    uint8_t nalu_t = (frame->payload[0] >> 1) & 0x3f;
    if(nalu_t < 36 ) { // Atlas streams: only log time for ACL NAL units
        ad_recv.push_back(get_current_time());
    }
    (void)uvg_rtp::frame::dealloc_frame(frame);
}
static void ovd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    uint8_t nalu_t = (frame->payload[0] >> 1) & 0x3f;
    if (nalu_t < 32 || nalu_t > 34) { // HEVC streams: only log time for VCL NAL units
        ovd_recv.push_back(get_current_time());
    }
    (void)uvg_rtp::frame::dealloc_frame(frame);
}
static void gvd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    uint8_t nalu_t = (frame->payload[0] >> 1) & 0x3f;
    if (nalu_t < 32 || nalu_t > 34) { // HEVC streams: only log time for VCL NAL units
        gvd_recv.push_back(get_current_time());
    }
    (void)uvg_rtp::frame::dealloc_frame(frame);
}
static void avd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    uint8_t nalu_t = (frame->payload[0] >> 1) & 0x3f;
    if (nalu_t < 32 || nalu_t > 34) { // HEVC streams: only log time for VCL NAL units
        avd_recv.push_back(get_current_time());
    }
    (void)uvg_rtp::frame::dealloc_frame(frame);
}

static int sender(std::string input_file, std::string local_address, int local_port, 
    std::string remote_address, int remote_port, float fps)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = 0;
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, false);

    size_t len = 0;
    void* mem = get_mem(input_file, len);
    if (mem == nullptr) {
        return EXIT_FAILURE;
    }
    v3c_file_map mmap;

    mmap_v3c_file((char*)mem, len, mmap);
    char* cbuf = (char*)mem;
    std::cout << "Starting latency send test with VPCC file" << std::endl;
    
    streams.ad->install_receive_hook(nullptr, ad_hook);
    streams.ovd->install_receive_hook(nullptr, ovd_hook);
    streams.gvd->install_receive_hook(nullptr, gvd_hook);
    streams.avd->install_receive_hook(nullptr, avd_hook);

    /* Start sending data */
    std::unique_ptr<std::thread> ad_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.ad, cbuf, V3C_AD, fps, mmap.ad_units, std::ref(ad_send)));

    std::unique_ptr<std::thread> ovd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.ovd, cbuf, V3C_OVD, fps, mmap.ovd_units, std::ref(ovd_send)));

    std::unique_ptr<std::thread> gvd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.gvd, cbuf, V3C_GVD, fps, mmap.gvd_units, std::ref(gvd_send)));

    std::unique_ptr<std::thread> avd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.avd, cbuf, V3C_AVD, fps, mmap.avd_units, std::ref(avd_send)));


    if (ad_thread && ad_thread->joinable())
    {
        ad_thread->join();
    }
    if (ovd_thread && ovd_thread->joinable())
    {
        ovd_thread->join();
    }
    if (gvd_thread && gvd_thread->joinable())
    {
        gvd_thread->join();
    }
    if (avd_thread && avd_thread->joinable())
    {
        avd_thread->join();
    }

    // just so we don't exit before last frame has arrived. Does not affect results
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); 
    
    sess->destroy_stream(streams.ad);
    sess->destroy_stream(streams.ovd);
    sess->destroy_stream(streams.gvd);
    sess->destroy_stream(streams.avd);
    rtp_ctx.destroy_session(sess);

    // Check for frame loss first
    std::cout << "ad_send size " << ad_send.size() << " ad_recv size " << ad_recv.size() << std::endl;
    std::cout << "ovd_send size " << ovd_send.size() << " ovd_recv size " << ovd_recv.size() << std::endl;
    std::cout << "gvd_send size " << gvd_send.size() << " gvd_recv size " << gvd_recv.size() << std::endl;
    std::cout << "avd_send size " << avd_send.size() << " avd_recv size " << avd_recv.size() << std::endl;

    if(ad_send.size() !=  ad_recv.size() ||
        ovd_send.size() !=  ovd_recv.size() ||
        gvd_send.size() !=  gvd_recv.size() ||
        avd_send.size() !=  avd_recv.size() ) 
    {
        std::cout << "Frame loss, ignore results" << std::endl;
        write_latency_results_to_file("latency_results", 0, 0, 0, 0);
        return EXIT_SUCCESS;
    }

    // No frame loss -> total number of transferred FULL frames is equal to ad_send.size() (or any other)
    int full_frames = ad_send.size();
    float total_time = 0;
    for (auto i = 1; i < full_frames; ++i) {
        // Find the time when a full frame was sent. For GVD and AVD its every fourth NAL unit
        auto full_frame_send_time = find_earliest_time_point(ad_send.at(i - 1),
        ovd_send.at(i - 1),
        gvd_send.at((i-1)*4),
        avd_send.at((i-1)*4));

        // Find the time when reception of a full frame was completed
        auto full_frame_recv_time = find_latest_time_point(ad_recv.at(i),
        ovd_recv.at(i),
        gvd_recv.at(i*4),
        avd_recv.at(i*4));

        long long diff_between_full_frames = full_frame_recv_time - full_frame_send_time;
        total_time += diff_between_full_frames;
    }
    std::cout << "full frames " << full_frames << ", total time " << total_time << std::endl;
    write_latency_results_to_file("latency_results", full_frames, total_time / (float)full_frames, 0, 0);

    std::cout << "Ending latency send test with " << full_frames << " full frames received" << std::endl;

    return EXIT_SUCCESS;
}

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, int fmt, float fps, const std::vector<v3c_unit_info> &units,
    std::vector<long long> &send_times)
{
    uint64_t current_frame = 0;
    uint64_t temp_nalu = 0;
    uint64_t period = (uint64_t)((1000 * 1000 / fps) );
    uint8_t* bytes = (uint8_t*)cbuf;
    rtp_error_t ret = RTP_OK;
    bool param_set = false; // For parameter set NAL units

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    for (auto& p : units) {
        for (auto& i : p.nal_infos) {
            param_set = false;
            uint8_t nalu_t = (bytes[i.location] >> 1) & 0x3f;
            if(fmt == V3C_AD && nalu_t > 35 ) {
                param_set = true;
            }
            else if (nalu_t >= 32 && nalu_t <= 34) {
                param_set = true;
            }
            if(!param_set) {  // Only log send times for non-parameter set NAL units
                send_times.push_back(get_current_time());
            }
            if ((ret = stream->push_frame(bytes + i.location, i.size, RTP_NO_H26X_SCL)) != RTP_OK) {
                std::cout << "Failed to send RTP frame!" << std::endl;
            }
            if(param_set) { // If this is a parameter set NALU, immediately send the next NAL unit
                continue;
            }
            temp_nalu++;
            if (fmt == V3C_GVD || fmt == V3C_AVD) { // If this is GVD or AVD stream, send 4 frames as fast as we can, then wait for frame interval
                if(temp_nalu < 4) {
                    continue;
                }
                temp_nalu = 0;
                current_frame += 1;
            }
            else {
                current_frame += 1;
            }

            // wait until is the time to send next latency test frame
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count();

            if (runtime < current_frame * period)
                std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
        }
    }
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
    //bool vvc_enabled           = get_vvc_state(argv[7]);
    //bool srtp_enabled          = get_srtp_state(argv[8]);

    return sender(input_file, local_address, local_port, remote_address, remote_port, fps);
}