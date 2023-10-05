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

void sender_thread(void* mem, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, int fps, bool vvc, bool srtp, 
    const std::string result_file, std::vector<uint64_t> chunk_sizes);

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    std::atomic<uint64_t> &net_bytes_sent, int fps, const std::string result_file);

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

    int nthreads               = atoi(argv[7]);
    int fps                    = atoi(argv[8]);
    bool vvc_enabled           = get_vvc_state(argv[9]);
    bool atlas_enabled         = get_atlas_state(argv[9]);
    bool srtp_enabled          = get_srtp_state(argv[10]);

    std::cout << "Starting uvgRTP sender tests. " << local_address << ":" << local_port
        << "->" << remote_address << ":" << remote_port << std::endl;

    size_t len   = 0;
    void *mem    = get_mem(input_file, len);
    if(atlas_enabled) {
        v3c_file_map mmap;
        mmap_v3c_file((char*)mem, len, mmap);
        if(mem == nullptr) {
            return EXIT_FAILURE;
        }
        uvgrtp::context ctx;
        uvgrtp::session* sess = ctx.create_session(remote_address, local_address);
        std::atomic<uint64_t> bytes_sent;
        int rce_flags = RCE_PACE_FRAGMENT_SENDING;
        rtp_flags_t rtp_flags = RTP_NO_H26X_SCL;
        v3c_streams streams = init_v3c_streams(sess, local_port, remote_port, rce_flags, false);
        sender_func(streams.ad, (char*)mem, mmap.ad_units, rtp_flags, V3C_AD, bytes_sent, fps, result_file);
    }
    else {
        std::vector<uint64_t> chunk_sizes;
        get_chunk_sizes(get_chunk_filename(input_file), chunk_sizes);

        if (mem == nullptr || chunk_sizes.empty())
        {
            std::cerr << "Failed to get file: " << input_file << std::endl;
            std::cerr << "or chunk location file: " << get_chunk_filename(input_file) << std::endl;
            return EXIT_FAILURE;
        }

        std::vector<std::thread*> threads;

        for (int i = 0; i < nthreads; ++i) {
            threads.push_back(new std::thread(sender_thread, mem, local_address, local_port, remote_address, 
                remote_port, i, fps, vvc_enabled, srtp_enabled, result_file, chunk_sizes));
        }

        for (unsigned int i = 0; i < threads.size(); ++i) {
            if (threads[i]->joinable())
            {
                threads[i]->join();
            }
            delete threads[i];
            threads[i] = nullptr;
        }

        threads.clear();
    }
    return EXIT_SUCCESS;
}

void sender_thread(void* mem, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, int fps, bool vvc, bool srtp, 
    const std::string result_file, std::vector<uint64_t> chunk_sizes)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;
    uint16_t thread_local_port = local_port + thread_num * 2;
    uint16_t thread_remote_port = remote_port + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &send, remote_address, local_address,
        thread_local_port, thread_remote_port, srtp, vvc, false, false);

    send->configure_ctx(RCC_FPS_NUMERATOR, fps);

    size_t bytes_sent = 0;
    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 / (double)fps) * 1000);
    rtp_error_t ret = RTP_OK;

    // start the sending test
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (auto& chunk_size : chunk_sizes)
    {
        if ((ret = send->push_frame((uint8_t*)mem + bytes_sent, chunk_size, 0)) != RTP_OK) {

            fprintf(stderr, "push_frame() failed!\n");

            // there is probably something wrong with the benchmark setup if push_frame fails
            std::cerr << "Send test push failed! Please fix benchmark suite." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            cleanup_uvgrtp(rtp_ctx, session, send);
            return;
        }

        bytes_sent += chunk_size;
        current_frame += 1;

        auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start
            ).count();

        // this enforces the fps restriction by waiting until it is time to send next frame
        // if this was eliminated, the test would be just about sending as fast as possible.
        // if the library falls behind, it is allowed to catch up if it can do it.
        if (runtime < current_frame * period)
            std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
    }

    // here we take the time and see how long it actually
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    write_send_results_to_file(result_file, bytes_sent, diff);
    cleanup_uvgrtp(rtp_ctx, session, send);
}

void sender_func(uvgrtp::media_stream* stream, const char* cbuf, const std::vector<v3c_unit_info> &units, rtp_flags_t flags, int fmt,
    std::atomic<uint64_t> &net_bytes_sent, int fps, const std::string result_file)
{
    stream->configure_ctx(RCC_FPS_NUMERATOR, fps);
    stream->configure_ctx(RCC_UDP_SND_BUF_SIZE, 40 * 1000 * 1000);

    size_t bytes_sent = 0;
    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 / (double)fps) * 1000);
    rtp_error_t ret = RTP_OK;

    // start the sending test
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (auto& p : units) {
        for (auto i : p.nal_infos) {

            //std::cout << "Sending NAL unit in location " << i.location << " with size " << i.size << std::endl;
            if((ret = stream->push_frame((uint8_t*)cbuf + i.location, i.size, flags)) != RTP_OK) {
                fprintf(stderr, "push_frame() failed!\n");

                // there is probably something wrong with the benchmark setup if push_frame fails
                std::cerr << "Send test push failed! Please fix benchmark suite." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return;
            }
            bytes_sent += i.size;
            current_frame += 1;
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start
            ).count();

            // this enforces the fps restriction by waiting until it is time to send next frame
            // if this was eliminated, the test would be just about sending as fast as possible.
            // if the library falls behind, it is allowed to catch up if it can do it.
            if (runtime < current_frame * period) {
                std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
            }
        }
    }
    // here we take the time and see how long it actually
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    write_send_results_to_file(result_file, bytes_sent, diff);
}