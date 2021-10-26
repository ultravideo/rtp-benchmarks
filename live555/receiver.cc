#include <liveMedia/liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include "sink.hh"

#include <cstdlib>

int main(int argc, char **argv)
{
    (void)argc, (void)argv;

    if (argc != 8) {
        fprintf(stderr, "usage: ./%s <local address> <local port> <remote address> <remote port> \
            <number of threads> <format> <srtp>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string local_address = argv[1];
    int local_port = atoi(argv[2]);
    std::string remote_address = argv[3];
    int remote_port = atoi(argv[4]);

    int nThreads = atoi(argv[5]);
    std::string format = argv[6];

    if (format != "hevc" && format != h265)
    {
        std::cerr << "Unsupported Live555 receiver format: " << format << std::endl;
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
