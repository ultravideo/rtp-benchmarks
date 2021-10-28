#pragma once

#include <uvgrtp/lib.hh>

#include <iostream>

#define KEY_SIZE   16
#define SALT_SIZE  14

constexpr int EXPECTED_FRAMES = 602;

void intialize_uvgrtp(uvgrtp::context& rtp_ctx, uvgrtp::session** session, uvgrtp::media_stream** mStream,
    std::string remote_address, std::string local_address, uint16_t local_port, uint16_t remote_port, 
    bool srtp, bool vvc, bool bind)
{
    int flags = 0;

    if (!bind)
    {
        flags = flags | RCE_ONLY_SEND;
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

    (*session) = rtp_ctx.create_session(remote_address, local_address);
    (*mStream) = (*session)->create_stream(
        local_port,
        remote_port,
        fmt,
        flags
    );

     /* Here UDP send/recv buffers are increased to 40MB
     * and frame delay is set 150 milliseconds to allow frames to arrive a little late */
    (*mStream)->configure_ctx(RCC_UDP_RCV_BUF_SIZE, 40 * 1000 * 1000);
    (*mStream)->configure_ctx(RCC_UDP_SND_BUF_SIZE, 40 * 1000 * 1000);
    (*mStream)->configure_ctx(RCC_PKT_MAX_DELAY, 150);

    std::cout << "Created a media_stream: " << local_address << ":" << local_port << "<->" 
        << remote_address << ":" << remote_port << std::endl;

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