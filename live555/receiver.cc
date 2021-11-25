#include <liveMedia/liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include "sink.hh"

#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: ./%s <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    result_filename = argv[1];
    std::string local_address = argv[2];
    int local_port = atoi(argv[3]);
    std::string remote_address = argv[4];
    int remote_port = atoi(argv[5]);

    int nthreads = atoi(argv[6]);
    bool vvc_enabled = get_vvc_state(argv[7]);
    bool srtp_enabled = get_srtp_state(argv[8]);

    if (vvc_enabled || srtp_enabled)
    {
        std::cerr << "Unsupported option for Live555 tester" << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp = false; // TODO

    TaskScheduler *scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment *env    = BasicUsageEnvironment::createNew(*scheduler);

    Port rtpPort(local_port);
    struct in_addr dst_addr;
    dst_addr.s_addr = our_inet_addr(local_address.c_str());
    Groupsock rtpGroupsock(*env, dst_addr, rtpPort, 255);

    OutPacketBuffer::maxSize = 40 * 1000 * 1000;

    RTPSource *source = H265VideoRTPSource::createNew(*env, &rtpGroupsock, 96);
    RTPSink_ *sink    = new RTPSink_(*env);

    sink->startPlaying(*source, nullptr, nullptr);
    env->taskScheduler().doEventLoop();

    return EXIT_SUCCESS;
}
