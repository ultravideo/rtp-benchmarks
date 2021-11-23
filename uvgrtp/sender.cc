#include "uvgrtp_util.hh"
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
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp, 
    const std::string result_file, std::vector<uint64_t> chunk_sizes);

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "usage: ./%s <input file> <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file  = argv[1];
    std::string result_file = argv[2];

    std::string local_address = argv[3];
    int local_port            = atoi(argv[4]);
    std::string remote_address = argv[5];
    int remote_port            = atoi(argv[6]);

    int nthreads = atoi(argv[7]);
    int fps      = atoi(argv[8]);
    std::string format = argv[9];
    std::string srtp   = argv[10];

    bool vvc = false;

    if (format == "vvc" || format == "h266")
    {
        vvc = true;
    }
    else if (format != "hevc" && format != "h265")
    {
        std::cerr << "Unsupported uvgRTP sender format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp_enabled = false;

    if (srtp == "1" || srtp == "yes" || srtp == "y" || srtp == "srtp")
    {
        srtp_enabled = true;
    }

    std::cout << "Starting uvgRTP sender tests. " << local_address << ":" << local_port
        << "->" << remote_address << ":" << remote_port << std::endl;

    size_t len   = 0;
    void *mem    = get_mem(input_file, len);

    std::vector<uint64_t> chunk_sizes;
    get_chunk_locations(get_chunk_filename(input_file), chunk_sizes);

    if (mem == nullptr || chunk_sizes.empty())
    {
        std::cerr << "Failed to get file: " << input_file << std::endl;
        std::cerr << "or chunk location file: " << get_chunk_filename(input_file) << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<std::thread*> threads;

    for (int i = 0; i < nthreads; ++i) {
        threads.push_back(new std::thread(sender_thread, mem, local_address, local_port, remote_address, 
            remote_port, i, fps, vvc, srtp_enabled, result_file, chunk_sizes));
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

    return EXIT_SUCCESS;
}

void sender_thread(void* mem, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp, 
    const std::string result_file, std::vector<uint64_t> chunk_sizes)
{
    uvgrtp::context rtp_ctx;
    uvgrtp::session* session = nullptr;
    uvgrtp::media_stream* send = nullptr;
    uint16_t thread_local_port = local_port + thread_num * 2;
    uint16_t thread_remote_port = remote_port + thread_num * 2;

    intialize_uvgrtp(rtp_ctx, &session, &send, remote_address, local_address,
        thread_local_port, thread_remote_port, vvc, srtp);

    size_t bytes_sent = 0;
    uint64_t current_frame = 0;
    uint64_t period = (uint64_t)((1000 / (float)fps) * 1000);
    size_t offset = 0;
    rtp_error_t ret = RTP_OK;

    // start the sending test
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (auto& chunk_size : chunk_sizes)
    {
        if ((ret = send->push_frame((uint8_t*)mem + offset, chunk_size, 0)) != RTP_OK) {

            fprintf(stderr, "push_frame() failed!\n");

            // there is probably something wrong with the benchmark setup if push_frame fails
            std::cerr << "Send test push failed! Please fix benchmark suite." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            cleanup_uvgrtp(rtp_ctx, session, send);
            return;
        }

        offset += chunk_size;
        bytes_sent += chunk_size;

        auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start
            ).count();

        // this enforces the fps restriction by waiting until it is time to send next frame
        // if this was eliminated, the test would be just about sending as fast as possible.
        // if the library falls behind, it is allowed to catch up if it can do it.
        current_frame += 1;
        if (runtime < current_frame * period)
            std::this_thread::sleep_for(std::chrono::microseconds(current_frame * period - runtime));
    }

    // here we take the time and see how long it actually
    auto end = std::chrono::high_resolution_clock::now();
    uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    write_send_results_to_file(result_file, bytes_sent, diff);
    cleanup_uvgrtp(rtp_ctx, session, send);
}