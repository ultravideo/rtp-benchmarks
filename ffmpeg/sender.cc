#include "../util/util.hh"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <stdbool.h>
}
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <string>
#include <iostream>

#define WIDTH  3840
#define HEIGHT 2160

std::atomic<int> nready(0);

void thread_func(void* mem, std::string local_address, uint16_t local_port,
    std::string remote_address, uint16_t remote_port, int thread_num, double fps, bool vvc, bool srtp,
    const std::string result_file, std::vector<uint64_t> chunk_sizes)
{
    char addr[64] = { 0 };
    enum AVCodecID codec_id = AV_CODEC_ID_H265;
    AVCodec *codec;
    AVCodecContext *c = NULL;

    int i;
    int ret;
    int x;
    int y;
    int got_output;

    AVFrame *frame;
    AVPacket pkt;

    codec = avcodec_find_encoder(codec_id);
    c = avcodec_alloc_context3(codec);

    av_log_set_level(AV_LOG_PANIC);

    c->width = HEIGHT;
    c->height = WIDTH;
    c->time_base.num = 1;
    c->time_base.den = fps;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->codec_type = AVMEDIA_TYPE_VIDEO;
    c->flags = AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_open2(c, codec, NULL);

    frame = av_frame_alloc();
    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;
    ret = av_image_alloc(frame->data, frame->linesize, c->width, c->height,
        c->pix_fmt, 32);

    AVFormatContext* avfctx;
    AVOutputFormat* fmt = av_guess_format("rtp", NULL, NULL);

    /* snprintf(addr, 64, "rtp://10.21.25.2:%d", 8888 + thread_num); */
    snprintf(addr, 64, "rtp://" + remote_address.c_str() + ": % d", remote_port + thread_num*2);
    ret = avformat_alloc_output_context2(&avfctx, fmt, fmt->name, addr);

    avio_open(&avfctx->pb, avfctx->filename, AVIO_FLAG_WRITE);

    struct AVStream* stream = avformat_new_stream(avfctx, codec);
    /* stream->codecpar->bit_rate = 400000; */
    stream->codecpar->width = WIDTH;
    stream->codecpar->height = HEIGHT;
    stream->codecpar->codec_id = AV_CODEC_ID_HEVC;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->time_base.num = 1;
    stream->time_base.den = fps;

    (void)avformat_write_header(avfctx, NULL);

    uint64_t chunk_size = 0;
	uint64_t current_frame = 0;
	uint64_t period  = (uint64_t)((1000 / (float)fps) * 1000);
    size_t bytes_sent = 0;

	std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (auto& chunk_size : chunk_sizes)
    {
        av_init_packet(&pkt);
        pkt.data = (uint8_t*)mem + bytes_sent;
        pkt.size = chunk_size;

        av_interleaved_write_frame(avfctx, &pkt);
        av_packet_unref(&pkt);

        ++current_frame;
        bytes_sent += chunk_size;

        auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start
            ).count();

        if (runtime < current_frame * period)
            std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));
    }

    auto end = std::chrono::high_resolution_clock::now();
    uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    write_send_results_to_file(result_file, bytes_sent, diff);

    nready++;

    avcodec_close(c);
    av_free(c);
    av_freep(&frame->data[0]);
    av_frame_free(&frame);
}

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "usage: ./%s <input file> <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file = argv[1];
    std::string result_file = argv[2];

    std::string local_address = argv[3];
    int local_port = atoi(argv[4]);
    std::string remote_address = argv[5];
    int remote_port = atoi(argv[6]);

    int nthreads = atoi(argv[7]);
    int fps = atoi(argv[8]);
    bool vvc_enabled = get_vvc_state(argv[9]);
    bool srtp_enabled = get_srtp_state(argv[10]);

    std::cout << "Starting FFMpeg sender tests. " << local_address << ":" << local_port
        << "->" << remote_address << ":" << remote_port << std::endl;

    avcodec_register_all();
    av_register_all();
    avformat_network_init();

    size_t len   = 0;
    void *mem    = get_mem(input_file, len);

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
        threads.push_back(new std::thread(thread_func, mem, local_address, local_port, remote_address,
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

    return EXIT_SUCCESS;
}
