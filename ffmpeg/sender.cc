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

extern void* get_mem(std::string filename, size_t& len);

#define WIDTH  3840
#define HEIGHT 2160

std::atomic<int> nready(0);

void thread_func(void *mem, size_t len, std::string remote_address, uint16_t port, int thread_num, double fps)
{
    char addr[64] = { 0 };
    enum AVCodecID codec_id = AV_CODEC_ID_H265;
    AVCodec *codec;
    AVCodecContext *c = NULL;
    int i, ret, x, y, got_output;
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
    snprintf(addr, 64, "rtp://" + remote_address.c_str() + ": % d", port + thread_num*2);
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

    uint64_t chunk_size, total_size;
    uint64_t fpt_ms  = 0;
    uint64_t fsize   = 0;
    uint32_t frames  = 0;
    uint64_t diff    = 0;
	uint64_t current = 0;
	uint64_t period  = (uint64_t)((1000 / (float)fps) * 1000);
	
	std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    for (size_t rounds = 0; rounds < 1; ++rounds) {
        for (size_t i = 0; i < len; ) {
            memcpy(&chunk_size, (uint8_t *)mem + i, sizeof(uint64_t));

            i          += sizeof(uint64_t);
            total_size += chunk_size;

            av_init_packet(&pkt);
            pkt.data = (uint8_t *)mem + i;
            pkt.size = chunk_size;

            av_interleaved_write_frame(avfctx, &pkt);
            av_packet_unref(&pkt);
			
            auto runtime = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start
            ).count();
			
			if (runtime < current * period)
				std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));

            frames++;
			current++;
            i += chunk_size;
            fsize += chunk_size;
        }
    }
    diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start
    ).count();

    fprintf(stderr, "%lu bytes, %lu kB, %lu MB took %lu ms %lu s\n",
        fsize, fsize / 1000, fsize / 1000 / 1000,
        diff, diff / 1000
    );

end:
    nready++;

    avcodec_close(c);
    av_free(c);
    av_freep(&frame->data[0]);
    av_frame_free(&frame);
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]);

    int nThreads = atoi(argv[5]);
    double fps = atof(argv[6]);
    std::string format = argv[7];

    if (format != "hevc" && format != h265)
    {
        std::cerr << "Unsupported FFmpeg sender format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp = false; // TODO

    avcodec_register_all();
    av_register_all();
    avformat_network_init();

    size_t len   = 0;
    void *mem    = get_mem("test_file.hevc", len);
    int nthreads = atoi(argv[5]);
    std::thread **threads = (std::thread **)malloc(sizeof(std::thread *) * nthreads);

    for (int i = 0; i < nthreads; ++i)
        threads[i] = new std::thread(thread_func, mem, len, remote_address, remote_port, i, fps);

    while (nready.load() != nthreads)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    for (int i = 0; i < nthreads; ++i) {
        threads[i]->join();
        delete threads[i];
    }
    free(threads);

}
