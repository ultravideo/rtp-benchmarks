#include "../util/util.hh"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <iostream>

#define SETUP_FFMPEG_PARAMETERS


struct thread_info {
    size_t pkts;
    size_t bytes;
    std::chrono::high_resolution_clock::time_point start;
} *thread_info;

std::atomic<int> nready(0);
std::chrono::high_resolution_clock::time_point last;
size_t pkts = 0;

static int cb(void *ctx)
{
    if (pkts) {
        uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - last
        ).count();

        /* we haven't received a frame in the last 300 milliseconds, stop receiver */
        if (diff >= 10)
            return 1;
    }

    return 0;
}

static const AVIOInterruptCB int_cb = { cb, NULL };

void thread_func(int thread_num, int nthreads, std::string local_address, int local_port,
    std::string remote_address, int remote_port, bool vvc, bool srtp)
{
    AVFormatContext *format_ctx = avformat_alloc_context();
    AVCodecContext *codec_ctx = NULL;
    int video_stream_index = 0;

    /* register everything */
    av_register_all();
    avformat_network_init();

    format_ctx->interrupt_callback = int_cb;

    av_log_set_level(AV_LOG_PANIC);

    /* open rtsp */
    AVDictionary *d = NULL;
    av_dict_set(&d, "protocol_whitelist", "file,udp,rtp", 0);

    char buf[256];

    /* input buffer size */
    snprintf(buf, sizeof(buf), "%d", 40 * 1000 * 1000);
    av_dict_set(&d, "buffer_size", buf, 32);

#ifdef SETUP_FFMPEG_PARAMETERS
    snprintf(buf, sizeof(buf), "%d", 10000000);
    av_dict_set(&d, "max_delay", buf, 32);

    snprintf(buf, sizeof(buf), "%d", 40 * 1000 * 1000);
    av_dict_set(&d, "recv_buffer_size", buf, 32);

    snprintf(buf, sizeof(buf), "%d", 40 * 1000 * 1000);
    av_dict_set(&d, "rcvbuf", buf, 32);

    /* avioflags flags (input/output)
     *
     * Possible values:
     *   ‘direct’ 
     *      Reduce buffering. */
    snprintf(buf, sizeof(buf), "direct");
    av_dict_set(&d, "avioflags", buf, 32);

    /* Reduce the latency introduced by buffering during initial input streams analysis. */
    av_dict_set(&d, "nobuffer", NULL, 32);

    /* Set probing size in bytes, i.e. the size of the data to analyze to get stream information.
     *
     * A higher value will enable detecting more information in case it is dispersed into the stream,
     * but will increase latency. Must be an integer not lesser than 32. It is 5000000 by default. */
    snprintf(buf, sizeof(buf), "%d", 32);
    av_dict_set(&d, "probesize", buf, 32);

    /*  Set number of frames used to probe fps. */
    snprintf(buf, sizeof(buf), "%d", 2);
    av_dict_set(&d, "fpsprobesize", buf, 32);

    snprintf(buf, sizeof(buf), "%d", 1);
    av_dict_set(&d, "stimeout", buf, 32);

    snprintf(buf, sizeof(buf), "%d", 1);
    av_dict_set(&d, "timeout", buf, 32);

    snprintf(buf, sizeof(buf), "%d", 1);
    av_dict_set(&d, "rw_timeout", buf, 32);
#endif

    if (!strcmp(local_address.c_str(), "127.0.0.1"))
        snprintf(buf, sizeof(buf), "ffmpeg/sdp/localhost/hevc_%d.sdp", nthreads);
    else
        snprintf(buf, sizeof(buf), "ffmpeg/sdp/lan/hevc_%d.sdp", nthreads);

    if (avformat_open_input(&format_ctx, buf, NULL, &d)) {
        fprintf(stderr, "failed to open input file\n");
        nready++;
        return;
    }

    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        fprintf(stderr, "failed to find stream info!\n");
        nready++;
        return;
    }

    for (size_t i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }

    size_t size = 0;
    AVPacket packet;
    av_init_packet(&packet);

    std::chrono::high_resolution_clock::time_point start;
    start = std::chrono::high_resolution_clock::now();

    /* start reading packets from stream */
    av_read_play(format_ctx);

    while (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index)
            size += packet.size;

        av_free_packet(&packet);
        av_init_packet(&packet);

        pkts += 1;
        last  = std::chrono::high_resolution_clock::now();
    }

    if (pkts == 598) {
        fprintf(stderr, "%zu %zu %zu\n", size, pkts,
            std::chrono::duration_cast<std::chrono::milliseconds>(last - start).count()
        );
    } else {
        fprintf(stderr, "discard %zu %zu %zu\n", size, pkts,
            std::chrono::duration_cast<std::chrono::milliseconds>(last - start).count()
        );
    }

    av_read_pause(format_ctx);
    nready++;
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string result_filename = argv[1];
    std::string local_address = argv[2];
    int local_port = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port = atoi(argv[5]);

    int nthreads = atoi(argv[6]);
    bool vvc_enabled = get_vvc_state(argv[7]);
    bool srtp_enabled = get_srtp_state(argv[8]);

    thread_info  = (struct thread_info *)calloc(nthreads, sizeof(*thread_info));

    std::vector<std::thread*> threads = {};

    for (int i = 0; i < nthreads; ++i) {
        threads.push_back(new std::thread(thread_func, i, nthreads, local_address, local_port,
            remote_address, remote_port, vvc_enabled, srtp_enabled));
    }

    // wait all the thread executions to end and delete them
    for (int i = 0; i < nthreads; ++i) {
        if (threads[i]->joinable())
        {
            threads[i]->join();
        }
        delete threads[i];
        threads[i] = nullptr;
    }

    /*
    for (int i = 0; i < nthreads; ++i)
        new std::thread(thread_func, argv[1], i * 2);

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    */
    return EXIT_SUCCESS;
}
