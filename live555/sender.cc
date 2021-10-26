#include <liveMedia/liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include "source.hh"
#include "H265VideoStreamDiscreteFramer.hh"

#include <cstdlib>

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
    int fps = atoi(argv[6]);
    std::string format = argv[7];

    if (format != "hevc" && format != "h265")
    {
        std::cerr << "Unsupported Live555 sender format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    bool srtp = false; // TODO
    
    TaskScheduler *scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment *env = BasicUsageEnvironment::createNew(*scheduler);
    H265FramedSource* framedSource = H265FramedSource::createNew(*env, remote_address.c_str());
    H265VideoStreamDiscreteFramer* framer = H265VideoStreamDiscreteFramer::createNew(*env, framedSource);

    Port rtpPort(remote_port);
    struct in_addr dst_addr;
    dst_addr.s_addr = our_inet_addr(remote_address.c_str());

    Groupsock rtpGroupsock(*env, dst_addr, rtpPort, 255);

    OutPacketBuffer::maxSize = 40 * 1000 * 1000;

    RTPSink* videoSink = H265VideoRTPSink::createNew(*env, &rtpGroupsock, 96);
    videoSink->startPlaying(*framer, NULL, videoSink);
    env->taskScheduler().doEventLoop();

    return EXIT_SUCCESS;
}
