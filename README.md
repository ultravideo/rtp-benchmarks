# RTP Benchmarks

This repository was created to compare the video streaming performance of uvgRTP against state-of-the-art in video streaming. The chosen libraries were Live555 and FFMpeg. Gstreamer also has the necessary features (supports HEVC RTP payload), but was omitted because there was no straightforward way to integrate its closely-knit media processing filters into this benchmark. This framework is not under active development and it can be a bit rough around the edges, but simple bugs may be fixed if the feature is needed.

Directories [uvgrtp](uvgrtp), [ffmpeg](ffmpeg), and [live555](live555) contain the C++ implementations for RTP (latency) senders and receivers. The Live555 implementation is has not been tested in a while and may not work out of the box. Linux is the only supported operating system.

The benchmarking includes four phases: 1) Network settings (`network.pl`), 2) file creation (`create.pl`), 3) running the benchmarks (`benchmark.pl`) and 4) parsing the results into a CSV file (`parse.pl`). All scripts print their options with the `--help` parameter.

## Requirements

* (kvazaar)[https://github.com/ultravideo/kvazaar] (required for generating the HEVC test file with `create.pl` script)
* A raw YUV420 video file (you can find sequences here: http://ultravideo.fi/#testsequences)
* uvgRTP (optional)
* Live555 (optional)
* FFmpeg (optional)

## Notes on used hardware

One core of a modern CPU can easily overload the capacity of 1 Gbps network, so it is recommended to do these test over a 10 Gbps, otherwise the network will be the limiting factor in higher resolutions and FPS values. For this reason we performed the tests using two computer equipped with Core i7-4770 and AMD Threadripper 2990WX CPUs connected via 10 Gbps LAN connection. 

## Phase 1: Network settings (optional)

If you intend to test a very quality stream at high fps values, you may need to increase transmit (TX) and receive (RX) queue lengths. We used value of 10000 for both. To increase the the TX queue length, issue the following command: `ifconfig <interface> txqueuelen 10000`. To increase the RX queue, issue the following command: `sysctl -w net.core.netdev_max_backlog=2000` or your system equivalent.

The RTP does not mandate the packet size, but the HEVC and VVC RTP specifications recommend using smaller packets that the MTU size. While local network usually support larger packet size without IP level fragmentation, only the MTU size of 1500 is guaranteed to be supported over the internet.

This corresponds to using the RTP packet size of 1458. The problem in tests with using smaller packet size is that the LAN will no achieve the same performance as with larger frames (our 10 Gbps LAN achieved 5.64 Gbps performance). 

This repository includes a test script called `network.pl` to test the maximal network performance on any packet size. This script is not mandatory for running the tests, but can help you desing the best test setup for your situation.

To run the network sender:
```
./network.pl \
   --role sender \
   --address <remote address> \
   --port 9000 \
   --psize 1458 \
```

To run the network receiving end:
```
./network.pl \
   --role receiver \
   --address <local address> \
   --port 9000 \
   --psize 1458 \
```

You must at least specify the `--role` for both ends and the sender also requires `--address` parameter for the destination address. Other parameters have default values.

## Phase 2: Creating the test file

Since the type of content has only a small impact on the RTP performance, the benchmark has been hardcoded to use a file with specific resolution (3840x2160) and amount of frames. You can get a suitable raw YUV420 file with the following command:

```
curl http://ultravideo.fi/video/Beauty_3840x2160_120fps_420_8bit_YUV_RAW.7z --output Beauty_4K.yuv.7z
7za e Beauty_4K.yuv.7z
```

In order to run the benchmarking, an encoded HEVC file as well as a support file are needed. The support file follows has the same name and a slightly different extension and it contains the sizes of the chunks in the encoded file. You can get both with `create.pl` script:

```
./create.pl \
   --input Beauty_4K.yuv \
   --resolution 3840x2160 \
   --qp 27 \
   --framerate 120 \
   --intra-period 64 \
   --preset medium \
```

`--input` is the only mandatory parameter.

There used to be a way to create a VVC file for testing, but due to unfortunate circumstances the script was lost. The VVC support needs a new script that would go through an existing VVC file and record the sizes of frames as 64-bit unsigned integers. The filename should follow format: `<vvc video file name>.m<extension>`. Since the VVC RTP format is very close to HEVC RTP format, they behave very similarly. If you however need to test the VVC performance, the script can be recreated.

## Phase 3: Running the benchmarks

This framework offers benchmarking for goodput (framerate) and latency.

### Goodput benchmarking

The benchmark is constructed in such a way that the library sends frames at specified FPS values, repeating each test number of times. This benchmark run is analyzed for timing, goodput and CPU usage as well as for lost frames. This is used to find the point at which the library cannot keep up with the exceeding fps values. This test can also be ran at multiple simultanous threads.

Individual values (`--fps` parameter) or a range (`--start`, `--end` and `--step` parameters) can be used the specify the FPS values tested. Without the `--step` variable, the FPS is doubled for each test.

For ffmpeg configuration, you must edit the .sdp files in ffmpeg/sdp/lan with your ip address.

When running the tests, start the sender first and the start will be synchronized when the receiver is started. 

Goodput sender
```
./benchmark.pl \
   --lib uvgrtp \
   --role send \
   --file filename.hevc \
   --saddr <local address> \
   --raddr <remote address> \
   --port 9999 \
   --threads 3 \
   --start 30 \
   --end 480 \
   --iter 20
```

Goodput receiver
```
./benchmark.pl \
   --lib uvgrtp \
   --role recv \
   --saddr <remote address> \
   --raddr <local address> \
   --port 9999 \
   --threads 3 \
   --start 30 \
   --end 480 \
   --iter 20
```

The results can be found in the `<lib>/results` folder which is created by the benchmark.pl script. Each individual test will create its own file within the folder which lists the parameters used. You can find the sender results on the sender computer and the receiver results on the receiver computer. When combined, these results can be parsed into a summmary of all tests.

### Latency benchmarking

The latency benchmarks measure the round-trip latency of Intra and Inter frames as well as the overall average frame latency. Latency benchmark sends the packet from sender and the receiver sends the packet back immediately. Remember to start the sender before you start the receiver.

Latency sender example:
```
./benchmark.pl \
   --lib uvgrtp \
   --role send \
   --latency
   --saddr <local address> \
   --raddr <remote address> \
   --port 9999
```

Latency receiver example:
```
./benchmark.pl \
   --lib uvgrtp \
   --role recv \
   --latency \
   --saddr <remote address> \
   --raddr <local address> \
   --port 9999
```

The latency results will only appear in the sending end. These too can be parsed into a summary with `parse.pl` script.

## Phase 4: Parsing the benchmark results

The `parse.pl` script can generate a CSV file from the goodput benchmarks for easier analysis and calculate the average latencies of latency test runs.

### Parsing Goodput results into a CSV file

In order to generate a CSV file of the results, you need to transfer the send and receive results to the same folder. After this has been done, you give this folder with `--path` parameter to the `parse.pl` script. Make sure the library name is included somewhere in the path or provide the library with `--lib` parameter. You need to also provide the size of the encoded file with `--filesize` parameter. You can get the filesize with `ls -l` command. Here is the simplest usage case for parsing the full goodput results:

```
./parse.pl \
    --path uvgrtp/results \
    --parse=csv \
    --filesize 7495852
```

If you haven't renamed the files and the result file matches the pattern `.*(send|recv).*(\d+)threads.*(\d+)fps.*(\d+)rounds.*` you don't
have to provide `--role` `--threads` or `--iter` parameters. 

It is also possible to parse individual files, find the best configuration or print the results, but the CSV file should suite most needs.

### Calculate averages latencies for inter, intra and all frames

Latencies takes in just one file at a time in `--path` parameter. This is how you get the average latencies from the benchmarks:

```
./parse.pl \
    --path uvgrtp/results/latencies_hevc_RTP_30fps_10rounds \
    --parse=latency
```

## Papers

A version of this framework has been used in the following [paper](https://researchportal.tuni.fi/en/publications/open-source-rtp-library-for-high-speed-4k-hevc-video-streaming):

```A. Altonen, J. Räsänen, J. Laitinen, M. Viitanen, and J. Vanne, “Open-Source RTP Library for High-Speed 4K HEVC Video Streaming”, in Proc. IEEE Int. Workshop on Multimedia Signal Processing, Tampere, Finland, Sept. 2020.```
