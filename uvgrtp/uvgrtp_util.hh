#pragma once

#include <uvgrtp/lib.hh>

#include <iostream>

#define KEY_SIZE   16
#define SALT_SIZE  14

constexpr int EXPECTED_FRAMES = 602;

void intialize_uvgrtp(uvgrtp::context& rtp_ctx, uvgrtp::session** session, uvgrtp::media_stream** mStream,
    std::string remote_address, std::string local_address, uint16_t local_port, uint16_t remote_port, 
    bool srtp, bool vvc, bool optimize_latency, bool atlas)
{
    int flags = RCE_NO_FLAGS;

    if (!optimize_latency)
    {
        flags |= RCE_PACE_FRAGMENT_SENDING;
    }

    if (srtp)
    {
        flags = flags | RCE_SRTP | RCE_SRTP_KMNGMNT_USER;
    }

    rtp_format_t fmt = RTP_FORMAT_H265;

    if (vvc)
    {
        fmt = RTP_FORMAT_H266;
    }
    if(atlas) {
        fmt = RTP_FORMAT_ATLAS;
    }

    (*session) = rtp_ctx.create_session(remote_address, local_address);
    (*mStream) = (*session)->create_stream(
        local_port,
        remote_port,
        fmt,
        flags
    );

    //std::cout << "Created a media_stream: " << local_address << ":" << local_port << "<->" 
    //    << remote_address << ":" << remote_port << std::endl;

    if (srtp)
    {
        uint8_t key[KEY_SIZE] = { 0 };
        uint8_t salt[SALT_SIZE] = { 0 };

        for (int i = 0; i < KEY_SIZE; ++i)
            key[i] = i + 7;
        for (int i = 0; i < SALT_SIZE; ++i)
            key[i] = i + 13;

        (*mStream)->add_srtp_ctx(key, salt);
    }

    /* Here UDP send/recv buffers are increased to 40MB
     * and frame delay is set 150 milliseconds to allow frames to arrive a little late */
    (*mStream)->configure_ctx(RCC_UDP_RCV_BUF_SIZE, 40 * 1000 * 1000);
    (*mStream)->configure_ctx(RCC_RING_BUFFER_SIZE, 40 * 1000 * 1000);
    (*mStream)->configure_ctx(RCC_UDP_SND_BUF_SIZE, 40 * 1000 * 1000);
    (*mStream)->configure_ctx(RCC_PKT_MAX_DELAY, 1000);
    (*mStream)->configure_ctx(RCC_POLL_TIMEOUT, 1000);
}

void cleanup_uvgrtp(uvgrtp::context& rtp_ctx, uvgrtp::session* session, uvgrtp::media_stream* mStream)
{
    if (session)
    {
        if (mStream)
        {
            session->destroy_stream(mStream);
        }

        rtp_ctx.destroy_session(session);
    }
}

long long get_current_time() {
    auto time = std::chrono::high_resolution_clock::now();
    auto since_epoch = std::chrono::time_point_cast<std::chrono::microseconds>(time);
    auto duration = since_epoch.time_since_epoch();
    return duration.count();
}

long long find_earliest_time_point(
    const long long& t1,
    const long long& t2,
    const long long& t3,
    const long long& t4) {

    return std::min(std::min(std::min(t1, t2), t3), t4);
}

long long find_latest_time_point(
    const long long& t1,
    const long long& t2,
    const long long& t3,
    const long long& t4) {

    return std::max(std::max(std::max(t1, t2), t3), t4);
}