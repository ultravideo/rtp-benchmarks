.PHONY: all clean

CXX = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++11 -g

test_file_creation: util/test_file_creation.cc util/util.cc
	$(CXX) $(CXXFLAGS) -o test_file_creation util/test_file_creation.cc util/util.cc -lkvazaar -lpthread 

uvgrtp_sender: uvgrtp/sender.cc util/util.cc
	$(CXX) $(CXXFLAGS) -o uvgrtp/sender uvgrtp/sender.cc util/util.cc uvgrtp/v3c_util.cc -luvgrtp -lpthread -lcryptopp 

uvgrtp_receiver: uvgrtp/receiver.cc util/util.cc
	$(CXX) $(CXXFLAGS) -o uvgrtp/receiver uvgrtp/receiver.cc util/util.cc uvgrtp/v3c_util.cc -luvgrtp -lpthread -lcryptopp

uvgrtp_latency_sender: uvgrtp/latency_sender.cc util/util.cc
	$(CXX) $(CXXFLAGS) -o uvgrtp/latency_sender uvgrtp/latency_sender.cc util/util.cc uvgrtp/v3c_util.cc -luvgrtp -lpthread -lcryptopp 

uvgrtp_latency_receiver: uvgrtp/latency_receiver.cc util/util.cc
	$(CXX) $(CXXFLAGS) -o uvgrtp/latency_receiver uvgrtp/latency_receiver.cc util/util.cc uvgrtp/v3c_util.cc -luvgrtp -lpthread -lcryptopp 

# ffmpeg_sender:
# 	$(CXX) $(CXXFLAGS) -Wno-unused -Wno-deprecated-declarations -Wno-unused-result -o ffmpeg/sender \
# 		ffmpeg/sender.cc util/util.cc `pkg-config --libs libavformat` -lpthread

ffmpeg_sender: ffmpeg/sender.cc util/util.cc
	$(CXX) $(CXXFLAGS) -Wno-unused -Wno-deprecated-declarations -Wno-unused-result -o ffmpeg/sender \
		ffmpeg/sender.cc util/util.cc -lavformat -lavcodec -lswscale -lz -lavutil  -lpthread 

ffmpeg_receiver: ffmpeg/receiver.cc util/util.cc
	$(CXX) $(CXXFLAGS) -Wno-unused -Wno-deprecated-declarations -Wno-unused-result -o ffmpeg/receiver \
		ffmpeg/receiver.cc util/util.cc -lavformat -lavcodec -lswscale -lz -lavutil -lpthread

ffmpeg_latency_sender: ffmpeg/latency_sender.cc util/util.cc
	$(CXX) $(CXXFLAGS) -Wno-unused -Wno-deprecated-declarations -Wno-unused-result -o ffmpeg/latency_sender \
		ffmpeg/latency_sender.cc util/util.cc  `pkg-config --libs libavformat` -lpthread

ffmpeg_latency_receiver: ffmpeg/latency_receiver.cc util/util.cc
	$(CXX) $(CXXFLAGS) -Wno-unused -Wno-deprecated-declarations -Wno-unused-result -o ffmpeg/latency_receiver \
		ffmpeg/latency_receiver.cc util/util.cc  -lavformat -lavcodec -lswscale -lz -lavutil -lpthread

live555_sender: live555/sender.cc live555/source.cc util/util.cc
	$(CXX) $(CXXFLAGS) live555/sender.cc live555/source.cc util/util.cc -o live555/sender \
		-I /usr/local/include/liveMedia \
		-I /usr/local/include/groupsock  \
		-I /usr/local/include/BasicUsageEnvironment \
		-I /usr/local/include/UsageEnvironment \
		-lpthread -lliveMedia -lgroupsock -lBasicUsageEnvironment \
		-lUsageEnvironment -lcrypto -lssl

live555_receiver: live555/receiver.cc live555/sink.cc util/util.cc
	$(CXX) $(CXXFLAGS) live555/receiver.cc live555/sink.cc util/util.cc -o live555/receiver \
		-I /usr/local/include/liveMedia \
		-I /usr/local/include/groupsock  \
		-I /usr/local/include/BasicUsageEnvironment \
		-I /usr/local/include/UsageEnvironment \
		-lpthread -lliveMedia -lgroupsock -lBasicUsageEnvironment \
		-lUsageEnvironment -lcrypto -lssl

live555_latency_sender: live555/latency_sender.cc util/util.cc
	$(CXX) $(CXXFLAGS) live555/latency_sender.cc util/util.cc -o live555/latency_sender \
		-I /usr/local/include/liveMedia \
		-I /usr/local/include/groupsock  \
		-I /usr/local/include/BasicUsageEnvironment \
		-I /usr/local/include/UsageEnvironment \
		-lpthread -lliveMedia -lgroupsock -lBasicUsageEnvironment \
		-lUsageEnvironment -lcrypto -lssl

live555_latency_receiver: live555/latency_receiver.cc
	$(CXX) $(CXXFLAGS) live555/latency_receiver.cc -o live555/latency_receiver \
		-I /usr/local/include/liveMedia \
		-I /usr/local/include/groupsock  \
		-I /usr/local/include/BasicUsageEnvironment \
		-I /usr/local/include/UsageEnvironment \
		-lpthread -lliveMedia -lgroupsock -lBasicUsageEnvironment \
		-lUsageEnvironment -lcrypto -lssl

clean:
	rm -f uvgrtp/receiver uvgrtp/sender  uvgrtp/latency_sender uvgrtp/latency_receiver \
		ffmpeg/receiver ffmpeg/sender ffmpeg/latency_sender ffmpeg/latency_receiver \
		live555/receiver live555/sender live555/latency test_file_creation
