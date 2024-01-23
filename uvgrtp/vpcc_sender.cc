#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <iostream>
#include <vector>

struct stream_results { // Save stats of each stream
    size_t bytes_sent = 0;
    uint64_t start = 0;
    uint64_t end = 0;
};

// encryption parameters
enum Key_length{SRTP_128 = 128, SRTP_196 = 196, SRTP_256 = 256};
constexpr Key_length KEY_S = SRTP_256;
constexpr int KEY_SIZE_BYTES = KEY_S/8;
constexpr int SALT_S = 112;
constexpr int SALT_SIZE_BYTES = SALT_S/8;

bool srtp_enabled = false;

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, int fmt, float fps, const std::vector<v3c_unit_info> &units,
    stream_results &res);

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "usage: ./%s <input file> <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file     = argv[1];
    std::string result_file    = argv[2];

    std::string local_address  = argv[3];
    int local_port             = atoi(argv[4]);
    std::string remote_address = argv[5];
    int remote_port            = atoi(argv[6]);
    int fps                    = atoi(argv[8]);
    //int nthreads               = atoi(argv[7]);
    //bool vvc_enabled           = get_vvc_state(argv[9]);
    //bool atlas_enabled         = get_atlas_state(argv[9]);
    srtp_enabled                 = get_srtp_state(argv[10]);

    std::cout << "Starting uvgRTP V-PCC sender round. " << local_address << ":" << local_port
        << "->" << remote_address << ":" << remote_port << std::endl;

    uvgrtp::context rtp_ctx;
    uvgrtp::session* sess = rtp_ctx.create_session(remote_address, local_address);

    int flags = RCE_PACE_FRAGMENT_SENDING;
    if (srtp_enabled) {
        flags |= RCE_SRTP | RCE_SRTP_KMNGMNT_USER | RCE_SRTP_KEYSIZE_256;
    }
    v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, flags, false);

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

    size_t len = 0;
    void* mem = get_mem(input_file, len);
    if (mem == nullptr) {
        return EXIT_FAILURE;
    }
    v3c_file_map mmap;

    mmap_v3c_file((char*)mem, len, mmap);
    char* cbuf = (char*)mem;

    stream_results ad_r  = {0,0,0};
    stream_results ovd_r = {0,0,0};
    stream_results gvd_r = {0,0,0};
    stream_results avd_r = {0,0,0};

   // Sleep a moment to make sure that the receiver is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); 
    
        /* Start sending data */
    std::unique_ptr<std::thread> ad_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.ad, cbuf, V3C_AD, fps, mmap.ad_units, std::ref(ad_r)));

    std::unique_ptr<std::thread> ovd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.ovd, cbuf, V3C_OVD, fps, mmap.ovd_units, std::ref(ovd_r)));

    std::unique_ptr<std::thread> gvd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.gvd, cbuf, V3C_GVD, fps, mmap.gvd_units, std::ref(gvd_r)));

    std::unique_ptr<std::thread> avd_thread =
        std::unique_ptr<std::thread>(new std::thread(sender_func, streams.avd, cbuf, V3C_AVD, fps, mmap.avd_units, std::ref(avd_r)));


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
    sess->destroy_stream(streams.ad);
    sess->destroy_stream(streams.ovd);
    sess->destroy_stream(streams.gvd);
    sess->destroy_stream(streams.avd);
    rtp_ctx.destroy_session(sess);

    // Calculate results
    size_t total_bytes_sent = ad_r.bytes_sent + ovd_r.bytes_sent + gvd_r.bytes_sent + avd_r.bytes_sent;

    long long start = find_earliest_time_point(ad_r.start, ovd_r.start, gvd_r.start, avd_r.start);
    long long end   = find_latest_time_point(ad_r.end, ovd_r.end, gvd_r.end, avd_r.end);
    long long diff = end - start;

    write_send_results_to_file(result_file, total_bytes_sent, (uint64_t)diff);
    
    return EXIT_SUCCESS;
}

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, int fmt, float fps, const std::vector<v3c_unit_info> &units,
    stream_results &res)
{
    stream->configure_ctx(RCC_FPS_NUMERATOR, fps);
    stream->configure_ctx(RCC_UDP_SND_BUF_SIZE, 40 * 1000 * 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Wait a moment to make sure that the receiver is ready

    uint64_t temp_nalu = 0;
    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 / (double)fps) * 1000);
    uint8_t* bytes = (uint8_t*)cbuf;
    rtp_error_t ret = RTP_OK;
    bool param_set = false; // For parameter set NAL units, special treatment
    /* Sending logic goes as follows:
    -Check NAL unit type:
     -Parameter set NAL units: DO NOT log time, send unit and immediately proceed to the next NAL unit.
     -AD or OVD stream: Each frame is composed of a single NAL unit. Log time, send unit and wait for the remaining interval depending on frame rate
     -GVD or AVD stream: Each frame is composed of 4 NAL units. Log send time of first one and send the remaining 3 as fast
      as possible. Then wait for the interval. Note: The send times of the remaining 3 also get logged, but only the first one is currently used in
      calculations, as it is the "start" of the frame. */

    size_t bytes_sent = 0;
    // start the sending test
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    res.start = get_current_time();

    for (auto& p : units) {
        for (auto i : p.nal_infos) {
            param_set = false; // Check the type of this NAL unit
            uint8_t nalu_t = (bytes[i.location] >> 1) & 0x3f;
            if(fmt == V3C_AD && nalu_t > 35 ) { // Check if Atlas parameter set NAL unit 
                param_set = true;
            }
            else if (nalu_t >= 32 && nalu_t <= 34) { // Check if video parameter set NAL unit 
                param_set = true;
            }
            if ((ret = stream->push_frame(bytes + i.location, i.size, RTP_NO_H26X_SCL)) != RTP_OK) {
                std::cout << "Failed to send RTP frame!" << std::endl;
            }
            bytes_sent += i.size;
            if(param_set) { // If this is a parameter set NALU, immediately send the next NAL unit
                continue;
            }
            temp_nalu++; // temp_nalu used to count the temporary 4 NAL units that make up a GVD or AVD frame
            if (fmt == V3C_GVD || fmt == V3C_AVD) { // If this is GVD or AVD stream, send 4 frames as fast as we can, then wait for frame interval
                if(temp_nalu < 4) {
                    continue;
                }
                temp_nalu = 0;
                current_frame += 1;
            }
            else {
                temp_nalu = 0;
                current_frame += 1;
            }
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();

            // this enforces the fps restriction by waiting until it is time to send next frame
            // if this was eliminated, the test would be just about sending as fast as possible.
            // if the library falls behind, it is allowed to catch up if it can do it.
            if (runtime < current_frame * period) {
                std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
            }
        }
    }
    // here we take the time and see how long it actually
    //auto end = std::chrono::high_resolution_clock::now();
    res.end = get_current_time();
    res.bytes_sent = bytes_sent;
    //uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}