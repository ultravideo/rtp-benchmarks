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

std::chrono::high_resolution_clock::time_point frame_send_time;

size_t ad_frames = 0;
size_t ovd_frames = 0;
size_t gvd_frames = 0;
size_t avd_frames = 0;

size_t gvd_frames_temp = 0;
size_t avd_frames_temp = 0;

std::atomic<uint64_t> full_frames = 0;

std::vector<long long> ad_recv = {};
std::vector<long long> ovd_recv = {};
std::vector<long long> gvd_recv = {};
std::vector<long long> avd_recv = {};

void sender_func(const uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    float fps);

    /*void sender_func(uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    float fps, std::vector<long long>* send_times);*/

long long get_current_time() {
    auto time = std::chrono::high_resolution_clock::now();
    auto since_epoch = std::chrono::time_point_cast<std::chrono::milliseconds>(time);
    auto duration = since_epoch.time_since_epoch();
    return duration.count();
}

long long find_earliest_time_point(
    const long long& t1,
    const long long& t2,
    const long long& t3,
    const long long& t4) {

    return std::min(std::min(std::min(t1, t2), t3), t4);
}

long long find_latest_time_point(
    const long long& t1,
    const long long& t2,
    const long long& t3,
    const long long& t4) {

    return std::max(std::max(std::max(t1, t2), t3), t4);
}

static void ad_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    ad_frames++;
    ad_recv.push_back(get_current_time());
}
static void ovd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    ovd_frames++;
    ovd_recv.push_back(get_current_time());
}
static void gvd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    gvd_frames_temp++;
    if(gvd_frames_temp == gvd_frames + 4) {
        gvd_frames++;
        gvd_recv.push_back(get_current_time());
    }
}
static void avd_hook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;
    avd_frames_temp++;
    if(avd_frames_temp == avd_frames + 4) {
        avd_frames++;
        avd_recv.push_back(get_current_time());
    }
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
    std::cout << "Starting latency send test with VPCC file" << std::endl;
    
    streams.ad->install_receive_hook(nullptr, ad_hook);
    streams.ovd->install_receive_hook(nullptr, ovd_hook);
    streams.gvd->install_receive_hook(nullptr, gvd_hook);
    streams.avd->install_receive_hook(nullptr, avd_hook);

    std::vector<long long> ad_send = {};
    std::vector<long long> ovd_send = {};
    std::vector<long long> gvd_send = {};
    std::vector<long long> avd_send = {};
    
    void sender_func(uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    float fps);

    /* Start sending data */
    std::unique_ptr<std::thread> ad_thread = std::make_unique<std::thread>(sender_func, streams.ad, mem, mmap.ad_units, RTP_NO_FLAGS,
        V3C_AD, fps);

    std::unique_ptr<std::thread> ovd_thread = std::make_unique<std::thread>(sender_func, streams.ovd, mem, mmap.ovd_units, RTP_NO_H26X_SCL,
        V3C_OVD, fps);

    std::unique_ptr<std::thread> gvd_thread = std::make_unique<std::thread>(sender_func, streams.gvd, mem, mmap.gvd_units, RTP_NO_H26X_SCL,
        V3C_GVD, fps);

    std::unique_ptr<std::thread> avd_thread = std::make_unique<std::thread>(sender_func, streams.avd, mem, mmap.avd_units, RTP_NO_H26X_SCL,
        V3C_AVD, fps);


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

    /* calculate latencies */

    // Check for frame loss first
    if(ad_send.size() !=  ad_recv.size() ||
        ovd_send.size() !=  ovd_recv.size() ||
        gvd_send.size() !=  gvd_frames_temp ||
        avd_send.size() !=  avd_frames_temp ) 
    {
        std::cout << "Frame loss, ignore results" << std::endl;
        return EXIT_SUCCESS;
    }

    // No frame loss -> total number of transferred FULL frames is equal to ad_send.size() (or any other)
    int full_frames = ad_send.size();
    float total_time = 0;
    for (auto i = 0; i < full_frames; ++i) {
        // Find the time when a full frame was sent. For GVD and AVD its every fourth NAL unit
        auto full_frame_send_time = find_earliest_time_point(ad_send.at(i),
        ovd_send.at(i),
        gvd_send.at(i*4),
        avd_send.at(i*4));

        // Find the time when reception of a full frame was completed
        auto full_frame_recv_time = find_latest_time_point(ad_recv.at(i),
        ovd_recv.at(i),
        gvd_recv.at(i*4),
        ovd_recv.at(i*4));

        long long diff_between_full_frames = full_frame_send_time - full_frame_recv_time;
        total_time += diff_between_full_frames;

    }
    write_latency_results_to_file("latency_results", full_frames, total_time / (float)full_frames, 0, 0);

    std::cout << "Ending latency send test with " << full_frames << " full frames received" << std::endl;

    return EXIT_SUCCESS;
}

void sender_func(const uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    float fps)
{
    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 * 1000 / fps) );
    uint8_t* bytes = (uint8_t*)cbuf;
    rtp_error_t ret = RTP_OK;
    std::vector<long long> send_times = {};

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    for (auto& p : units) {
        for (auto i : p.nal_infos) {
            uint8_t nalu_t = (bytes[i.location] >> 1) & 0x3f;
            if(fmt == V3C_AD && nalu_t > 35 ) { // Atlas streams: Skip non-ACL NAL units
                continue;
            }
            else if (nalu_t >= 32 && nalu_t <= 34) { // HEVC streams: Skip parameter set NAL units
                continue;
            }
            send_times.push_back(get_current_time());
            if ((ret = stream->push_frame(bytes + i.location, i.size, RTP_NO_H26X_SCL)) != RTP_OK) {
                std::cout << "Failed to send RTP frame!" << std::endl;
            }

            if (fmt == V3C_GVD || fmt == V3C_AVD) { // If this is GVD or AVD stream, send 4 frames as fast as we can
                if((current_frame + 1) % 4 != 0) {
                    continue;
                }
            }
            current_frame += 1;

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
    bool vvc_enabled           = get_vvc_state(argv[7]);
    bool srtp_enabled          = get_srtp_state(argv[8]);

    return sender(input_file, local_address, local_port, remote_address, remote_port, fps);
}