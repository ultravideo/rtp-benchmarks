#include "latsource.hh"

#include "../util/util.hh"

#include <GroupsockHelper.hh>
#include <FramedSource.hh>

#include <chrono>
#include <mutex>
#include <queue>
#include <thread>

EventTriggerId H265LatencyFramedSource::eventTriggerId = 0;
unsigned H265LatencyFramedSource::referenceCount       = 0;


static uint8_t *buf;
static size_t offset    = 0;
static size_t bytes     = 0;
static uint64_t current = 0;
static uint64_t period  = 0;
static bool initialized = false;

std::mutex delivery_mtx;
std::queue<std::pair<size_t, uint8_t *>> nals;
std::chrono::high_resolution_clock::time_point s_tmr, e_tmr;

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

static std::pair<size_t, uint8_t *> find_next_nal(void)
{
    static size_t len         = 0;
    static uint8_t *p         = NULL;
    static uint8_t *end       = NULL;
    static uint8_t *nal_start = NULL;
    static uint8_t *nal_end   = NULL;

    if (!p) {
        p   = (uint8_t *)get_mem(input_file_, len);
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

    mtx_.lock();
    fprintf(stderr, "send frame\n");

    auto nal = find_next_nal();

    if (!nal.first || !nal.second) {
        e_tmr = std::chrono::high_resolution_clock::now();
        uint64_t diff = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(e_tmr - s_tmr).count();
        fprintf(stderr, "%lu bytes, %lu kB, %lu MB took %lu ms %lu s\n",
            bytes, bytes / 1000, bytes / 1000 / 1000,
            diff, diff / 1000
        );
        exit(EXIT_SUCCESS);
    }

    uint64_t runtime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - s_tmr
    ).count();

    if (runtime < current * period)
        std::this_thread::sleep_for(std::chrono::microseconds(current * period - runtime));

    /* try to hold fps for intra/inter frames only */
    if (nal.first > 1500)
        ++current;

    uint8_t *newFrameDataStart = nal.second;
    unsigned newFrameSize      = nal.first;

    bytes += newFrameSize;

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
