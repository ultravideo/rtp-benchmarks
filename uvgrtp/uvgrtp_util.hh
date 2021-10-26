#pragma once

#include <uvgrtp/lib.hh>

#define KEY_SIZE   16
#define SALT_SIZE  14

constexpr int EXPECTED_FRAMES = 602;

void intialize_uvgrtp(uvgrtp::context& rtp_ctx, uvgrtp::session** session, uvgrtp::media_stream** mStream,
    std::string remote_address, std::string local_address, uint16_t local_port, uint16_t remote_port, bool srtp, bool vvc)
{
    int flags = 0;
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