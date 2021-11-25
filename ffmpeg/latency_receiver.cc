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

#include <chrono>
#include <thread>
#include <string>
#include <atomic>

extern void *get_mem(int argc, char **argv, size_t& len);

#define WIDTH  3840
#define HEIGHT 2160
#define FPS     200

std::chrono::high_resolution_clock::time_point fs, fe;
std::atomic<bool> received(false);

struct ffmpeg_ctx {
    AVFormatContext *sender;
    AVFormatContext *receiver;
};

static ffmpeg_ctx *init_ffmpeg(std::string remote_address, int remote_port)
{
    avcodec_register_all();
    av_register_all();
    avformat_network_init();

    av_log_set_level(AV_LOG_PANIC);

    ffmpeg_ctx *ctx = new ffmpeg_ctx;
    enum AVCodecID codec_id = AV_CODEC_ID_H265;
    int i, ret, x, y, got_output;
    AVCodecContext *c = NULL;
    AVCodec *codec;
    AVFrame *frame;
    AVPacket pkt;

    codec = avcodec_find_encoder(codec_id);
    c = avcodec_alloc_context3(codec);

    c->width = HEIGHT;
    c->height = WIDTH;
    c->time_base.num = 1;
    c->time_base.den = FPS;
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

    AVOutputFormat *fmt = av_guess_format("rtp", NULL, NULL);
    char addr[64] = { 0 };
    snprintf(addr, 64, "rtp://" + remote_address.c_str() + ": % d", remote_port);
    ret = avformat_alloc_output_context2(&ctx->sender, fmt, fmt->name, addr);

    avio_open(&ctx->sender->pb, ctx->sender->filename, AVIO_FLAG_WRITE);

    struct AVStream* stream = avformat_new_stream(ctx->sender, codec);
    stream->codecpar->width = WIDTH;
    stream->codecpar->height = HEIGHT;
    stream->codecpar->codec_id = AV_CODEC_ID_HEVC;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->time_base.num = 1;
    stream->time_base.den = FPS;

    char buf[256];
    AVDictionary *d_s = NULL;
    AVDictionary *d_r = NULL;

    snprintf(buf, sizeof(buf), "%d", 40 * 1000 * 1000);
    av_dict_set(&d_s, "buffer_size", buf, 32);

    /* Flush the underlying I/O stream after each packet.
     *
     * Default is -1 (auto), which means that the underlying protocol will decide,
     * 1 enables it, and has the effect of reducing the latency,
     * 0 disables it and may increase IO throughput in some cases. */
    snprintf(buf, sizeof(buf), "%d", 1);
    av_dict_set(&d_s, "flush_packets", NULL, 32);

    /* Set maximum buffering duration for interleaving. The duration is expressed in microseconds,
     * and defaults to 10000000 (10 seconds).
     *
     * To ensure all the streams are interleaved correctly, libavformat will wait until it has
     * at least one packet for each stream before actually writing any packets to the output file.
     * When some streams are "sparse" (i.e. there are large gaps between successive packets),
     * this can result in excessive buffering.
     *
     * This field specifies the maximum difference between the timestamps of the first and
     * the last packet in the muxing queue, above which libavformat will output a packet regardless of
     * whether it has queued a packet for all the streams.
     *
     * If set to 0, libavformat will continue buffering packets until it has a packet for each stream,
     * regardless of the maximum timestamp difference between the buffered packets. */
    snprintf(buf, sizeof(buf), "%d", 1000);
    av_dict_set(&d_s, "max_interleave_delta", buf, 32);

    /* avioflags flags (input/output)
     *
     * Possible values:
     *   ‘direct’
     *      Reduce buffering. */
    snprintf(buf, sizeof(buf), "direct");
    av_dict_set(&d_s, "avioflags", buf, 32);

    (void)avformat_write_header(ctx->sender, &d_s);

    /* When sender has been initialized, initialize receiver */
    ctx->receiver = avformat_alloc_context();
    int video_stream_index;

    av_dict_set(&d_r, "protocol_whitelist", "file,udp,rtp", 0);

    /* input buffer size */
    snprintf(buf, sizeof(buf), "%d", 40 * 1000 * 1000);
    av_dict_set(&d_r, "buffer_size", buf, 32);

    /* avioflags flags (input/output)
     *
     * Possible values:
     *   ‘direct’
     *      Reduce buffering. */
    snprintf(buf, sizeof(buf), "direct");
    av_dict_set(&d_r, "avioflags", buf, 32);

    /* Reduce the latency introduced by buffering during initial input streams analysis. */
    av_dict_set(&d_r, "nobuffer", NULL, 32);

    /* Set probing size in bytes, i.e. the size of the data to analyze to get stream information.
     *
     * A higher value will enable detecting more information in case it is dispersed into the stream,
     * but will increase latency. Must be an integer not lesser than 32. It is 5000000 by default. */
    snprintf(buf, sizeof(buf), "%d", 32);
    av_dict_set(&d_r, "probesize", buf, 32);

    /*  Set number of frames used to probe fps. */
    snprintf(buf, sizeof(buf), "%d", 2);
    av_dict_set(&d_r, "fpsprobesize", buf, 32);

    ctx->receiver->flags = AVFMT_FLAG_NONBLOCK;

    if (!strcmp(remote_address.c_str(), "127.0.0.1"))
        snprintf(buf, sizeof(buf), "ffmpeg/sdp/localhost/lat_hevc.sdp");
    else
        snprintf(buf, sizeof(buf), "ffmpeg/sdp/lan/lat_hevc.sdp");

    if (avformat_open_input(&ctx->receiver, buf, NULL, &d_r) != 0) {
        fprintf(stderr, "nothing found!\n");
        return NULL;
    }

    if (avformat_find_stream_info(ctx->receiver, NULL) < 0) {
        fprintf(stderr, "stream info not found!\n");
        return NULL;
    }

    /* search video stream */
    for (size_t i = 0; i < ctx->receiver->nb_streams; i++) {
        if (ctx->receiver->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }

    return ctx;
}

static int receiver(std::string remote_address)
{
    AVPacket pkt;
    ffmpeg_ctx *ctx;

    if (!(ctx = init_ffmpeg(remote_address.c_str())))
        return EXIT_FAILURE;

    av_init_packet(&pkt);
    av_read_play(ctx->receiver);

    while (av_read_frame(ctx->receiver, &pkt) >= 0) {
        av_write_frame(ctx->sender, &pkt);
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]);
    bool vvc_enabled = get_vvc_state(argv[5]);
    bool srtp_enabled = get_srtp_state(argv[6]);

    return receiver(remote_address, remote_port);
}
