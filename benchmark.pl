#!/usr/bin/env perl

use warnings;
use strict;
use IO::Socket;
use IO::Socket::INET;
use Getopt::Long;

$| = 1; # autoflush

my $DEFAULT_ADDR = "127.0.0.1";
my $DEFAULT_PORT = 19500;

sub clamp {
    my ($start, $end) = @_;
    my @clamped = (0, 0);

    $clamped[0] = $start < 30   ? 30   : $start;
    $clamped[1] = $end   > 5000 ? 5000 : $end;

    return @clamped;
}

sub mk_ssock {
    my $s = IO::Socket::INET->new(
        LocalAddr => $_[0],
        LocalPort => $_[1],
        Proto     => "tcp",
        Type      => SOCK_STREAM,
        Listen    => 1,
    ) or die "Couldn't connect to $_[0]:$_[1]: $@\n";

    return $s;
}

sub mk_rsock {
    my $s = IO::Socket::INET->new(
        PeerAddr  => $_[0],
        PeerPort  => $_[1],
        Proto     => "tcp",
        Type      => SOCK_STREAM,
        Timeout   => 1,
    ) or die "Couldn't connect to $_[0]:$_[1]: $@\n";

    return $s;
}

sub send_benchmark {
    print "Starting send benchmark\n";

    my ($lib, $file, $saddr, $raddr, $port, $iter, $threads, $gen_recv, $e, $format, $srtp, @fps_vals) = @_;
    my ($socket, $remote, $data);
    my @execs = split ",", $e;

    print "Waiting for receiver to connect to our TCP socket\n";

    $socket = mk_ssock($saddr, $port);
    $remote = $socket->accept();

    my $result_directory = "./$lib/results";
    print "Checking existance of results folder $result_directory\n";

    unless(-e $result_directory or mkdir $result_directory) {
        die "Unable to create $result_directory\n";
    }

    print "Starting send benchmark for $lib\n";

    foreach (@execs) {
        my $exec = $_;

        unless(-e "./$lib/$exec") {
            die "The executable ./$lib/$exec has not been created! \n";
        }
        foreach ((1 .. $threads)) {
            my $thread = $_;
            foreach (@fps_vals) {

                my $fps = $_;
                my $logname = "send_$format" . "_RTP" . "_$thread" . "threads_$fps". "fps_$iter" . "rounds";
                if ($srtp)
                {
                    $logname = "send_$format" . "_SRTP" . "_$thread" . "threads_$fps". "fps_$iter" . "rounds";
                }

                my $result_file = "$lib/results/$logname";

                unlink $result_file if -e $result_file; # erase old results if they exist

                for ((1 .. $iter)) {
                    print "Starting to benchmark sending at $fps fps, round $_\n";
                    $remote->recv($data, 16);
                    my $exit_code = system ("(time ./$lib/$exec $file $result_file $saddr $port $raddr $port $thread $fps $format $srtp) 2>> $result_file");
                    $remote->send("end") if $gen_recv;
                    
                    die "Sender failed! \n" if ($exit_code ne 0);
                }
            }
        }
    }

    print "Send benchmark finished\n";
    $socket->close();
}

sub recv_benchmark {
    print "Receive benchmark\n";
    my ($lib, $saddr, $raddr, $port, $iter, $threads, $e, $format, $srtp, @fps_vals) = @_;
    
    print "Connecting to the TCP socket of the sender\n";
    my $socket = mk_rsock($saddr, $port);
    my @execs = split ",", $e;

    my $result_directory = "./$lib/results";
    print "Checking existance of results folder $result_directory\n";

    unless(-e $result_directory or mkdir $result_directory) {
        die "Unable to create $result_directory\n";
    }

    print "Starting receive benchmark for $lib\n";

    foreach (@execs) {
        my $exec = $_;

        unless(-e "./$lib/$exec") {
            die "The executable ./$lib/$exec has not been created! \n";
        }

        foreach ((1 .. $threads)) {
            my $thread = $_;
            foreach (@fps_vals) {
                my $fps = $_;
                my $logname = "recv_$format" . "_RTP" . "_$thread" . "threads_$fps". "fps_$iter" . "rounds";
                if ($srtp)
                {
                    $logname = "recv_$format" . "_SRTP" . "_$thread" . "threads_$fps". "fps_$iter" . "rounds";
                }

                my $result_file = "$lib/results/$logname";

                unlink $result_file if -e $result_file; # erase old results if they exist

                for ((1 .. $iter)) {
                    print "Starting to benchmark receive at $fps fps, round $_\n";
                    $socket->send("start"); # I believe this is used to avoid firewall from blocking traffic
                    # please note that the local address for receiver is raddr
                    my $exit_code = system ("(time ./$lib/receiver $raddr $port $saddr $port $thread $format $srtp) 2>> $result_file");
                    die "Receiver failed! \n" if ($exit_code ne 0);
                }
            }
        }
    }

    print "Receive benchmark finished\n";
    $socket->close();
}

# use netcat to capture the stream
sub recv_generic {
    print "Start netcat receiver\n";

    my ($lib, $addr, $port, $iter, $threads, @fps_vals) = @_;
    # my ($sfps, $efps) = clamp($start, $end);
    my $socket = mk_rsock($addr, $port);
    my $ports = "";

    # spawn N netcats using gnu parallel, send message to sender to start sending,
    # wait for message from sender that all the packets have been sent, sleep a tiny bit
    # move receiver output from separate files to one common file and proceed to next iteration
    $ports .= (8888 + $_ * 2) . " " for ((0 .. $threads - 1));

    while ($threads ne 0) {
        foreach (@fps_vals) {
            my $logname = "recv_results_$threads" . "threads_$_". "fps";
            system "parallel --files nc -kluvw 0 $addr ::: $ports &";
            $socket->send("start");
            $socket->recv(my $data, 16);
            sleep 1;
            system "killall nc";

            open my $fhz, '>>', "$lib/results/$logname";
            opendir my $dir, "/tmp";

            foreach my $of (grep (/par.+\.par/i, readdir $dir)) {
                print $fhz -s "/tmp/$of";
                print $fhz "\n";
                unlink "/tmp/$of";
            }
            closedir $dir;
        }

        $threads--;
    }

    print "End netcat receiver\n";
    $socket->close();
}

sub send_latency {
    
    my ($lib, $file, $saddr, $raddr, $port, $fps, $iter, $format, $srtp) = @_;
    my ($socket, $remote, $data);
    print "Latency send benchmark for $lib\n";
    
    unless(-e "./$lib/latency_sender") {
        die "The executable ./$lib/latency_sender has not been created! \n";
    }
    
    $socket = mk_ssock($saddr, $port);
    $remote = $socket->accept();
    
    my $logname = "latencies_$format" . "_RTP_$fps". "fps_$iter" . "rounds";
    if ($srtp)
    {
        $logname = "latencies_$format" . "_SRTP_$fps". "fps_$iter" . "rounds";
    }
    

    
    my $result_file = "$lib/results/$logname";
    unlink $result_file if -e $result_file; # erase old results if they exist
    
    for ((1 .. $iter)) {
        print "Latency send benchmark round $_" . "/$iter\n";
        $remote->recv($data, 16);
        
        my $exit_code = system ("./$lib/latency_sender $file $saddr $port $raddr $port $fps $format $srtp >> $result_file 2>&1");
        die "Latency sender failed! \n" if ($exit_code ne 0);
    }
    print "Latency send benchmark finished\n";
    $socket->close();
}

sub recv_latency {
    my ($lib, $saddr, $raddr, $port, $iter, $format, $srtp) = @_;
    print "Latency receive benchmark for $lib\n";
    
    unless(-e "./$lib/latency_receiver") {
        die "The executable ./$lib/latency_receiver has not been created! \n";
    }
    
    my $socket = mk_rsock($saddr, $port);
    
    for ((1 .. $iter)) {
        print "Latency receive benchmark round $_" . "/$iter\n";
        sleep 1; # 1 s, make sure the sender has managed to catch up
        $socket->send("start");
        
        my $exit_code = system ("./$lib/latency_receiver $raddr $port $saddr $port $format $srtp");
        die "Latency receiver failed! \n" if ($exit_code ne 0);
    }
    print "Latency receive benchmark finished\n";
    $socket->close();
}

# TODO explain every parameter
sub print_help {
    print "usage (benchmark):\n  ./benchmark.pl \n"
    . "\t--lib     <uvgrtp|ffmpeg|live555>\n"
    . "\t--role    <send|recv>\n"
    . "\t--file    <test filename>\n"
    . "\t--saddr   <sender address>\n"
    . "\t--raddr   <receiver address>\n"
    . "\t--port    <used port>\n"
    . "\t--threads <# of threads>\n"
    . "\t--start   <start fps>\n"
    . "\t--end     <end fps>\n\n";

    print "usage (latency):\n  ./benchmark.pl \n"
    . "\t--latency\n"
    . "\t--role <send|recv>\n"
    . "\t--saddr  <sender address>\n"
    . "\t--raddr  <receiver address>\n"
    . "\t--port   <used port>\n"
    . "\t--fps <the fps at which benchmarking is done>\n"
    . "\t--lib <uvgrtp|ffmpeg|live555>\n\n" and exit;
}

GetOptions(
    "library|lib|l=s"            => \(my $lib = ""),
    "role|r=s"                   => \(my $role = ""),
    "sender_addr|saddr=s"        => \(my $saddr = ""),
    "receiver_addr|raddr=s"      => \(my $raddr = ""),
    "port|p=i"                   => \(my $port = 0),
    "iterations|iter|i=i"        => \(my $iter = 10),
    "input|filename|file|in=s"   => \(my $file = ""),
    "threads|t=i"                => \(my $threads = 1),
    "start|s=f"                  => \(my $start = 0),
    "end|e=f"                    => \(my $end = 0),
    "step=i"                     => \(my $step = 0),
    "use-nc|use-netcat"          => \(my $nc = 0),
    "framerate|framerates|fps=s" => \(my $fps = ""),
    "latency|lat"                => \(my $lat = 0),
    "srtp"                       => \(my $srtp = 0),
    "exec=s"                     => \(my $exec = "default"),
    "format|form=s"              => \(my $format = ""),
    "help"                       => \(my $help = 0)
) or die "failed to parse command line!\n";

$port = $DEFAULT_PORT if !$port;
$saddr = $DEFAULT_ADDR if !$saddr;
$raddr = $DEFAULT_ADDR if !$raddr;

print_help() if ((!$start or !$end) and !$fps) and !$lat;
die "Please specify library with --lib" if !$lib;
die "Please specify role with --role" if !$role;


die "library not supported\n" if !grep (/$lib/, ("uvgrtp", "ffmpeg", "live555"));
die "format not supported\n"  if !grep (/$format/, ("hevc", "vvc", "h265", "h266"));

$fps = 30.0 if $lat and !$fps;

my @fps_vals = ();

if (!$lat) {
    if ($fps) {
        @fps_vals = split ",", $fps;
    } else {
        ($start, $end) = clamp($start, $end);
        for (my $i = $start; $i <= $end; ) {
            push @fps_vals, $i;

            if ($step) { $i += $step; }
            else       { $i *= 2; }
        }
    }
}

if ($role eq "send" or $role eq "sender") {
    die "Please specify test file with --file for sender" if !$file;
    
    if (!$format)
    {
        # try to detect the format from file extension
        $format = "hevc" if $file =~ /(.*\.hevc)/;
        $format = "hevc" if $file =~ /(.*\.h265)/;
        $format = "vvc" if $file =~ /(.*\.vvc)/;
        $format = "vvc" if $file =~ /(.*\.h266)/;
        
        die "Could not determine test file format. Set file extension or specify with --format" if !$format;
    }
    
    if ($lat) {
        system "make $lib" . "_latency_sender";
        send_latency($lib, $file, $saddr, $raddr, $port, $fps, $iter, $format, $srtp);
    } else {
        if ($exec eq "default") {
            system "make $lib" . "_sender";
            $exec = "sender";
        }
        send_benchmark($lib, $file, $saddr, $raddr, $port, $iter, $threads, $nc, $exec, $format, $srtp, @fps_vals);
    }
} elsif ($role eq "recv" or $role eq "receive" or $role eq "receiver") {
    die "Please specify test format with --format for receiver" if !$format;
    
    if ($lat) {
        system "make $lib" . "_latency_receiver";
        recv_latency($lib, $saddr, $raddr, $port, $iter, $format, $srtp);
    } elsif (!$nc) {
        if ($exec eq "default") {
            system "make $lib" . "_receiver";
            $exec = "receiver";
        }
        recv_benchmark($lib, $saddr, $raddr, $port, $iter, $threads, $exec, $format, $srtp, @fps_vals);
    } else {
        recv_generic($lib, $saddr, $port, $iter, $threads, @fps_vals);
    }
} else {
    print "invalid role: '$role'\n" and exit;
}
