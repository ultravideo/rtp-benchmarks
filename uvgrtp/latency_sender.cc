#include "uvgrtp_util.hh"
#include "v3c_util.hh"
#include "../util/util.hh"

#include <uvgrtp/lib.hh>
#include <uvgrtp/clock.hh>

#include <cstring>
#include <algorithm>
#include <string>
#include <chrono>

std::chrono::high_resolution_clock::time_point frame_send_time;

size_t frames   = 0;
size_t ninters  = 0;
size_t nintras  = 0;
size_t n_noncl  = 0;

size_t total       = 0;
size_t total_intra = 0;
size_t total_inter = 0;
size_t total_noncl = 0; // non-ACL or non-VCL unit (not video coding layer)

bool vvc_headers = false;
int total_frames_received = 0;
bool atlas_enabled = false;

uint64_t get_diff()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - frame_send_time
        ).count();
}

static void hook_sender(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
    (void)arg;

    if (frame) {

        uint64_t diff = 0;
        if (vvc_headers)
        {
            switch (frame->payload[2] & 0x3f) {
                case 19: // intra frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_intra += (diff / 1000);
                    nintras++;
                    frames++;
                    break;
                case 1: // inter frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_inter += (diff / 1000);
                    ninters++;
                    frames++;
                    break;
            }
        }
        else if (atlas_enabled) { // Note that this ignores any parameter set NAL units
            uint8_t nalu_t = (frame->payload[0] >> 1) & 0x3f;
            if (nalu_t <= 15) { // inter frame
                diff = get_diff();
                total += (diff / 1000);
                total_inter += (diff / 1000);
                ninters++;
                frames++;
            }
            else if (nalu_t >= 16 && nalu_t <= 29) { // intra frame
                diff = get_diff();
                total += (diff / 1000);
                total_intra += (diff / 1000);
                nintras++;
                frames++;
            }
            else { // non-ACL frame
                diff = get_diff();
                total += (diff / 1000);
                total_noncl += (diff / 1000);
                n_noncl++;
                //frames++; ignore non-ACL and non-VCL frames in tests
            }
        }
        else
        {
            switch ((frame->payload[4] >> 1) & 0x3f) {
                case 19: // intra frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_intra += (diff / 1000);
                    nintras++;
                    frames++;
                    break;
                case 1: // inter frame
                    diff = get_diff();
                    total += (diff / 1000);
                    total_inter += (diff / 1000);
                    ninters++;
                    frames++;
                    break;
            }
        }
        ++total_frames_received;
    }

}

static int sender(std::string input_file, std::string local_address, int local_port, 
    std::string remote_address, int remote_port, float fps, bool vvc_enabled, bool srtp_enabled, bool atlas)
{
    vvc_headers = vvc_enabled;

    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;

    intialize_uvgrtp(rtp_ctx, &session, &send, remote_address, local_address,
        local_port, remote_port, srtp_enabled, vvc_enabled, true, atlas);

    send->install_receive_hook(nullptr, hook_sender);

    size_t len = 0;
    void* mem = get_mem(input_file, len);
    std::vector<uint64_t> chunk_sizes; // For HEVC/VVC
    v3c_file_map mmap; // For Atlas

    if(atlas_enabled) {
        mmap_v3c_file((char*)mem, len, mmap);
        std::cout << "Starting latency send test with Atlas data" << std::endl;
    }
    else {
        get_chunk_sizes(get_chunk_filename(input_file), chunk_sizes);  
        if(chunk_sizes.empty()) {
            return EXIT_FAILURE;
        }
        std::cout << "Starting latency send test with " << chunk_sizes.size() << " chunks" << std::endl;
    }
    if (mem == nullptr)
    {
        return EXIT_FAILURE;
    }


    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 * 1000 / fps) );
    size_t offset = 0;
    rtp_error_t ret = RTP_OK;

    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    if(atlas_enabled) {
        for (auto& p : mmap.ad_units) {
            for (auto i : p.nal_infos) {
                // record send time
                frame_send_time = std::chrono::high_resolution_clock::now();
                if ((ret = send->push_frame((uint8_t*)mem + i.location, i.size, RTP_NO_H26X_SCL)) != RTP_OK) {
                    fprintf(stderr, "push_frame() failed!\n");
                    cleanup_uvgrtp(rtp_ctx, session, send);
                    return EXIT_FAILURE;
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
    else {
        for (auto& chunk_size : chunk_sizes)
        {
            // record send time
            frame_send_time = std::chrono::high_resolution_clock::now();
            if ((ret = send->push_frame((uint8_t*)mem + offset, chunk_size, 0)) != RTP_OK) {
                fprintf(stderr, "push_frame() failed!\n");
                cleanup_uvgrtp(rtp_ctx, session, send);
                return EXIT_FAILURE;
            }

            current_frame += 1;
            offset += chunk_size;

            // wait until is the time to send next latency test frame
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start).count();

            if (runtime < current_frame * period)
                std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
        }
    }
    

    // just so we don't exit before last frame has arrived. Does not affect results
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); 

    cleanup_uvgrtp(rtp_ctx, session, send);

    fprintf(stderr, "%zu: intra %lf, inter %lf, avg %lf\n",
        frames,
        total_intra / (float)nintras,
        total_inter / (float)ninters,
        total / (float)frames
    );
    write_latency_results_to_file("latency_results", frames, total_intra / (float)nintras, total_inter / (float)ninters,
        total / (float)frames);

    std::cout << "Ending latency send test with " << total_frames_received << " frames received" << std::endl;

    return EXIT_SUCCESS;
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
    atlas_enabled              = get_atlas_state(argv[7]);
    bool srtp_enabled          = get_srtp_state(argv[8]);

    return sender(input_file, local_address, local_port, remote_address, remote_port, fps, vvc_enabled, srtp_enabled, atlas_enabled);
}