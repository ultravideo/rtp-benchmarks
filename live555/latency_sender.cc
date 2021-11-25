#include "../util/util.hh"

#include <BasicUsageEnvironment.hh>
#include <FramedSource.hh>
#include <GroupsockHelper.hh>
#include <liveMedia/liveMedia.hh>
#include <RTPInterface.hh>

#include <chrono>
#include <climits>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <string>

#include "latsource.hh"
#include "sink.hh"

using namespace std::chrono;

#define BUFFER_SIZE 40 * 1000 * 1000

#define KEY(frame, size) ((((frame) % 64) << 26) + (size))

EventTriggerId H265LatencyFramedSource::eventTriggerId = 0;
unsigned H265LatencyFramedSource::referenceCount       = 0;

static uint64_t current = 0;
static uint64_t period  = 0;
static bool initialized = false;

static size_t frames  = 0;
static size_t nintras = 0;
static size_t ninters = 0;

static size_t intra_total = 0;
static size_t inter_total = 0;
static size_t frame_total = 0;

static std::mutex lat_mtx;
static std::queue<std::pair<size_t, uint8_t *>> nals;
static high_resolution_clock::time_point s_tmr, start;

typedef std::pair<high_resolution_clock::time_point, size_t> finfo;
static std::unordered_map<uint64_t, finfo> timestamps;

static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

const uint8_t *ff_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *out= ff_avc_find_startcode_internal(p, end);
    if (p < out && out < end && !out[-1]) out--;
    return out;
}

static std::pair<size_t, uint8_t *> find_next_nal(std::string input_file)
{
    static size_t len         = 0;
    static uint8_t *p         = NULL;
    static uint8_t *end       = NULL;
    static uint8_t *nal_start = NULL;
    static uint8_t *nal_end   = NULL;

    if (!p) {
        p   = (uint8_t *)get_mem(input_file, len);
        end = p + len;
        len = 0;

        nal_start = (uint8_t *)ff_avc_find_startcode(p, end);
    }

    while (nal_start < end && !*(nal_start++))
        ;

    if (nal_start == end)
        return std::make_pair(0, nullptr);

    nal_end    = (uint8_t *)ff_avc_find_startcode(nal_start, end);
    auto ret   = std::make_pair((size_t)(nal_end - nal_start), (uint8_t *)nal_start);
    len       += 4 + nal_end - nal_start;
    nal_start  = nal_end;

    return ret;
}

H265LatencyFramedSource *H265LatencyFramedSource::createNew(UsageEnvironment& env, std::string input_file)
{
    return new H265LatencyFramedSource(env, input_file);
}

H265LatencyFramedSource::H265LatencyFramedSource(UsageEnvironment& env, std::string input_file):
    FramedSource(env),
    input_file_(input_file)
{
    period = (uint64_t)((1000 / (float)30) * 1000);

    if (!eventTriggerId)
        eventTriggerId = envir().taskScheduler().createEventTrigger(deliverFrame0);
}

H265LatencyFramedSource::~H265LatencyFramedSource()
{
    if (!--referenceCount) {
        envir().taskScheduler().deleteEventTrigger(eventTriggerId);
        eventTriggerId = 0;
    }
}

void H265LatencyFramedSource::doGetNextFrame()
{
    if (!initialized) {
        s_tmr       = std::chrono::high_resolution_clock::now();
        initialized = true;
    }

    deliverFrame();
}

void H265LatencyFramedSource::deliverFrame0(void *clientData)
{
    ((H265LatencyFramedSource *)clientData)->deliverFrame();
}

void H265LatencyFramedSource::deliverFrame()
{
    if (!isCurrentlyAwaitingData())
        return;

    auto nal = find_next_nal(input_file_);

    if (!nal.first || !nal.second)
        return;

    uint64_t runtime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - s_tmr
    ).count();

    if (runtime < current * period)
        std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));

    /* try to hold fps for intra/inter frames only */
    if (nal.first > 1500)
        ++current;

    /* Start timer for the frame
     * RTP sink will calculate the time difference once the frame is received */
    /* printf("send frame %u, size %u\n", current, nal.first); */

    uint64_t key = nal.first;

    if (timestamps.find(key) != timestamps.end()) {
        fprintf(stderr, "cannot use size as timestamp for this frame!\n");
        exit(EXIT_FAILURE);
    }
    timestamps[key].first  = std::chrono::high_resolution_clock::now();
    timestamps[key].second = nal.first;

    uint8_t *newFrameDataStart = nal.second;
    unsigned newFrameSize      = nal.first;

    if (newFrameSize > fMaxSize) {
        fFrameSize = fMaxSize;
        fNumTruncatedBytes = newFrameSize - fMaxSize;
    } else {
        fFrameSize = newFrameSize;
    }

    fDurationInMicroseconds = 0;
    memmove(fTo, newFrameDataStart, fFrameSize);

    FramedSource::afterGetting(this);
}

static void thread_func(void)
{
    unsigned prev_frames = UINT_MAX;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));

        if (prev_frames == frames) {
            /* fprintf(stderr, "frame lost\n"); */
            break;
        }

        prev_frames = frames;
    }

    fprintf(stderr, "%zu: intra %lf, inter %lf, avg %lf\n",
        frames,
        intra_total / (float)nintras,
        inter_total / (float)ninters,
        frame_total / (float)frames
    );

    exit(EXIT_FAILURE);
}

RTPSink_::RTPSink_(UsageEnvironment& env):
    MediaSink(env)
{
    fReceiveBuffer = new uint8_t[BUFFER_SIZE];
}

RTPSink_::~RTPSink_()
{
}

void RTPSink_::uninit()
{
    stopPlaying();
    delete fReceiveBuffer;
}

void RTPSink_::afterGettingFrame(
    void *clientData,
    unsigned frameSize,
    unsigned numTruncatedBytes,
    struct timeval presentationTime,
    unsigned durationInMicroseconds
)
{
    ((RTPSink_ *)clientData)->afterGettingFrame(
        frameSize,
        numTruncatedBytes,
        presentationTime,
        durationInMicroseconds
    );
}

void RTPSink_::afterGettingFrame(
    unsigned frameSize,
    unsigned numTruncatedBytes,
    struct timeval presentationTime,
    unsigned durationInMicroseconds
)
{
    (void)frameSize,        (void)numTruncatedBytes;
    (void)presentationTime, (void)durationInMicroseconds;

    /* start loop that monitors activity and if there has been
     * no activity for 2s (same as uvgRTP) the receiver is stopped) */
    if (!frames)
        (void)new std::thread(thread_func);

    uint64_t diff;
    uint8_t nal_type;

    uint64_t key = frameSize;
    /* printf("recv frame %zu, size %u\n", frames + 1, frameSize); */

    if (timestamps.find(key) == timestamps.end()) {
        printf("frame %zu,%zu not found from set!\n", frames + 1, key);
        exit(EXIT_FAILURE);
    }

    if (timestamps[key].second != frameSize) {
        printf("frame size mismatch (%zu vs %zu)\n", timestamps[key].second, frameSize);
        exit(EXIT_FAILURE);
    }

    diff = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - timestamps[key].first
    ).count();
    timestamps.erase(key);

    nal_type = (fReceiveBuffer[0] >> 1) & 0x3f;

    if (nal_type == 19 || nal_type == 1) {
        if (nal_type == 19)
            nintras++, intra_total += (diff / 1000);
        else
            ninters++, inter_total += (diff / 1000);
        frame_total += (diff / 1000);
    }

    if (++frames == 601) {
        fprintf(stderr, "%zu: intra %lf, inter %lf, avg %lf\n",
            frames,
            intra_total / (float)nintras,
            inter_total / (float)ninters,
            frame_total / (float)frames
        );
        exit(EXIT_SUCCESS);
    }

    continuePlaying();
}

Boolean RTPSink_::continuePlaying()
{
    if (!fSource)
        return False;

    fSource->getNextFrame(
        fReceiveBuffer,
        BUFFER_SIZE,
        afterGettingFrame,
        this,
        onSourceClosure,
        this
    );

    return True;
}

static int sender(std::string input_file, std::string local_address, int local_port,
    std::string remote_address, int remote_port)
{
    (void)addr;

    H265VideoStreamDiscreteFramer *framer;
    H265LatencyFramedSource *framedSource;
    TaskScheduler *scheduler;
    UsageEnvironment *env;
    RTPSink *videoSink;
    RTPSource *source;
    RTPSink_ *sink;

    scheduler = BasicTaskScheduler::createNew();
    env       = BasicUsageEnvironment::createNew(*scheduler);

    OutPacketBuffer::maxSize = 40 * 1000 * 1000;

    Port send_port(remote_port);
    struct in_addr dst_addr;
    dst_addr.s_addr = our_inet_addr(remote_address.c_str());

    Port recv_port(local_port);
    struct in_addr src_addr;
    src_addr.s_addr = our_inet_addr(local_address.c_str());

    Groupsock send_socket(*env, dst_addr, send_port, 255);
    Groupsock recv_socket(*env, src_addr, recv_port, 255);

    /* sender */
    videoSink    = H265VideoRTPSink::createNew(*env, &send_socket, 96);
    framedSource = H265LatencyFramedSource::createNew(*env, input_file);
    framer       = H265VideoStreamDiscreteFramer::createNew(*env, framedSource);

    /* receiver */
    source = H265VideoRTPSource::createNew(*env, &recv_socket, 96);
    sink    = new RTPSink_(*env);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    videoSink->startPlaying(*framer, NULL, videoSink);
    sink->startPlaying(*source, nullptr, nullptr);
    env->taskScheduler().doEventLoop();

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <input file> <local address> <local port> <remote address> <remote port> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file = argv[1];

    std::string local_address = argv[2];
    int local_port = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port = atoi(argv[5]);

    float fps = atof(argv[6]);
    bool vvc_enabled = get_vvc_state(argv[7]);
    bool srtp_enabled = get_srtp_state(argv[8]);

    return sender(input_file, local_address, local_port, remote_address, remote_port);
}
