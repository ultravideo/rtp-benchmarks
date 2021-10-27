#include <liveMedia/liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include "source.hh"
#include "H265VideoStreamDiscreteFramer.hh"

#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc != 11) {
        fprintf(stderr, "usage: ./%s <input file> <result file> <local address> <local port> <remote address> <remote port> \
            <number of threads> <fps> <format> <srtp> \n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file = argv[1];
    std::string result_file = argv[2];

    std::string local_address = argv[3];
    int local_port = atoi(argv[4]);
    std::string remote_address = argv[5];
    int remote_port = atoi(argv[6]);

    int nthreads = atoi(argv[7]);
    int fps = atoi(argv[8]);
    std::string format = argv[9];
    std::string srtp = argv[10];

    if (format != "hevc" && format != "h265")
    {
        std::cerr << "Unsupported LIVE555 sender format: " << format << std::endl;
        return EXIT_FAILURE;
    }

    if (srtp == "1" || srtp == "yes" || srtp == "y")
    {
        std::cerr << "LIVE555 does not support SRTP" << std::endl;
        return EXIT_FAILURE;
    }
    
    TaskScheduler *scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment *env = BasicUsageEnvironment::createNew(*scheduler);
    H265FramedSource* framedSource = H265FramedSource::createNew(*env, fps, input_file, result_file);
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
