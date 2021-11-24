# RTP Benchmarks

This repository features the benchmarking of uvgRTP, FFMpeg and Live555 against each other.
Directories [uvgrtp](uvgrtp), [ffmpeg](ffmpeg), and [live555](live555) contain the C++ implementations for RTP (latency) senders and receivers. Currently, Linux is the only supported operating system.

The benchmarking includes four phases: 1) Network settings (network.pl), 2) file creation (create.pl), 3) running the benchmarks (benchmark.pl) and 4) parsing the results into a CSV file (parse.pl).

## Requirements

* kvazaar (required for generating the HEVC test file)
* A raw YUV420 video file (you can find sequences here: http://ultravideo.fi/#testsequences)
* uvgRTP (optional)
* Live555 (optional)
* FFmpeg (optional)

## Notes on used hardware

One core of a modern CPU can easily overload the capacity of 1 Gbps network, so it is recommended to do these test over a 10 Gbps, otherwise the network will be the limiting factor in higher resolutions and FPS values. For this reason we performed the tests using two computer equipped with Core i7-4770 and AMD Threadripper 2990WX CPUs connected via 10 Gbps LAN connection. 

## Phase 1: Network settings (optional)

In OS settings, you should increase socket write and read buffers as well as the TX and RX Queue lenghts if you intend to test high bitrate streams.

The RTP does not mandate the packet size, but the HEVC and VVC RTP specifications recommend using smaller packets that the MTU size. While local network usually support larger packet size without IP level fragmentation, only the MTU size of 1500 is guaranteed to be supported over the internet.

This corresponds to using the RTP packet size of 1458. The problem in tests with using smaller packet size is that the LAN will no achieve the same performance as with larger frames (in our 10 Gbps achieved 5.64 Gbps performance). 

This repository includes a test script called `network.pl` to test the maximal network performance on any packet size. This script is not mandatory for running the tests, but can help you desing the best test setup for your situation.

To run the network sender:
```
./network.pl \
   --role sender \
   --address 127.0.0.1 \
   --port 9000 \
   --psize 1458 \
```

To run the network receiving end:
```
./network.pl \
   --role receiver \
   --address 127.0.0.1 \
   --port 9000 \
   --psize 1458 \
```

Only role and address for sender are required parameters, others have default values.

## Phase 2: Creating the test file

Currently, the benchmark has been hardcoded to use a file with specific resolution (3840x2160) and amount of frames. You can get the designated raw YUV420 file with the following command:

```
curl http://ultravideo.fi/video/Beauty_3840x2160_120fps_420_8bit_YUV_RAW.7z --output Beauty_4K.yuv.7z
7za e Beauty_4K.yuv.7z
```

In order to run the benchmarking, a specially formatted file is needed. The test file can be created by `create.pl` script:

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

This framework offers benchmarking for goodput (framerate) and latency. There is also a netcat receiver to analyze the sender end, but this is not mean for benchmarking, only for validating part of the framework.

### Goodput benchmarking

The benchmarking can be done on wide variaty of different framerates. Individual values or a range can be used the specify the FPS values tested. Multiple simultanous threads can also be tested.

In the following example, each thread configuration will test all FPS values between the range 30 - 480 and and each FPS is tested 20 times. Without the step variable, FPS is doubled so the tested values are: 30, 60, 120, 240, 480

When running the tests, start the sender first and the start is synchronized when the receiver is started. Each FPS value for each thread configuration provides one log file.  The individual runs are synchronized using a separate TCP connection. You can find the sender results on the sender computer and the receiver results on the receiver computer.

Goodput sender
```
./benchmark.pl \
   --lib uvgrtp \
   --role send \
   --file filename.hevc \
   --saddr 127.0.0.1 \
   --raddr 127.0.0.1 \
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
   --addr 127.0.0.1 \
   --port 9999 \
   --threads 3 \
   --start 30 \
   --end 480 \
   --iter 20
```

### Netcat receiver (for testing)

Benchmark uvgRTP's send goodput using netcat

Using netcat to capture the stream requires [OpenBSD's netcat](https://github.com/openbsd/src/blob/master/usr.bin/nc/netcat.c)
and [GNU Parallel](https://www.gnu.org/software/parallel/)

Sender
```
./benchmark.pl \
   --lib uvgrtp \
   --role send \
   --use-nc \
   --addr 127.0.0.1 \
   --port 9999 \
   --threads 3 \
   --start 30 \
   --end 60 \
```

Receiver
```
./benchmark.pl \
   --lib uvgrtp \
   --role recv \
   --use-nc \
   --addr 127.0.0.1 \
   --port 9999 \
   --threads 3 \
   --start 30 \
   --end 60 \
```

### Latency benchmarking

Latency benchmark sends the packet from sender and the receiver sends the packet back immediately. Start the sender before you start the receiver.

Latency sender example:
```
./benchmark.pl \
   --lib uvgrtp \
   --role send \
   --latency
   --addr 127.0.0.1 \
   --port 9999
```

Latency receiver example:
```
./benchmark.pl \
   --lib uvgrtp \
   --role recv \
   --latency \
   --addr 127.0.0.1 \
   --port 9999
```


## Phase 4: Parsing the benchmark results

The `parse.pl` script can generate a CSV file from the goodput benchmarks for easier analysis and calculate the average latencies of latency test runs.

### Parsing Goodput results into a CSV file

In order to generate a CSV file of the results, you need to transfer the send and receive results to the same folder. Then you give this folder as a path to the script. Make sure the library name is included somewhere in the path or provide the library with `--lib` parameter. You can get the filesize with `ls -l` command. Here is the simplest usage case for parsing the full goodput results:

```
./parse.pl --path uvgrtp/results --parse=csv --filesize 7495852
```

If you haven't renamed the files and the result file matches the pattern `.*(send|recv).*(\d+)threads.*(\d+)fps.*(\d+)rounds.*` you don't
have to provide `--role` `--threads` or `--iter` parameters. 

It is also possible to parse individual files, find the best configuration or print the results, but the CSV file is the main usage scenario.

### Calculate averages latencies for inter, intra and all frames

Latencies takes in just one file at a time in `--path` parameter. This is how you get the average latencies from the benchmarks:

```
./parse.pl --path uvgrtp/results/latencies_hevc_RTP_30fps_10rounds --parse=latency
```

## Papers

A version of this framework has been used in the following [paper](https://researchportal.tuni.fi/en/publications/open-source-rtp-library-for-high-speed-4k-hevc-video-streaming):

```A. Altonen, J. Räsänen, J. Laitinen, M. Viitanen, and J. Vanne, “Open-Source RTP Library for High-Speed 4K HEVC Video Streaming”, in Proc. IEEE Int. Workshop on Multimedia Signal Processing, Tampere, Finland, Sept. 2020.```
