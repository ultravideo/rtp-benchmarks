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

    my ($lib, $saddr, $raddr, $port, $iter, $threads, $gen_recv, $e, $format, $srtp, @fps_vals) = @_;
    my ($socket, $remote, $data);
    my @execs = split ",", $e;

    print "Waiting for receiver to connect to our TCP socket\n";

    $socket = mk_ssock($saddr, $port);
    $remote = $socket->accept();

    my $result_directory = "./$lib/results";
    print "Creating results folder in $result_directory\n";

    unless(-e $result_directory or mkdir $result_directory) {
        die "Unable to create $result_directory\n";
    }

    print "Starting send benchmark\n";

    foreach (@execs) {
        my $exec = $_;

        unless(-e "./$lib/$exec") {
            die "The executable ./$lib/$exec has not been created! \n";
        }
        foreach ((1 .. $threads)) {
            my $thread = $_;
            foreach (@fps_vals) {

                my $fps = $_;
                my $logname = "send_results_$thread" . "threads_$fps". "fps_$iter" . "iter_$exec";

                my $result_file = "$lib/results/$logname";

                # remove result file if it exists
                if(-e "./$result_file") {
                    system("rm ./$result_file");
                }

                my $input_file = "test_file.hevc";

                for ((1 .. $iter)) {
                    print "Starting to benchmark sending at $fps fps, round $_\n";
                    $remote->recv($data, 16);
                    system ("(time ./$lib/$exec $input_file $result_file $saddr $port $raddr $port $thread $fps $format $srtp) 2>> $result_file");
                    $remote->send("end") if $gen_recv;
                }
            }
        }
    }

    print "Send benchmark finished\n";
}

sub recv_benchmark {
    print "Receive benchmark\n";
    my ($lib, $saddr, $raddr, $port, $iter, $threads, $e, $format, $srtp, @fps_vals) = @_;
    
    print "Connecting to the TCP socket of the sender\n";
    my $socket = mk_rsock($saddr, $port);
    my @execs = split ",", $e;

    my $result_directory = "./$lib/results";
    print "Creating results folder in $result_directory\n";

    unless(-e $result_directory or mkdir $result_directory) {
        die "Unable to create $result_directory\n";
    }

    print "Starting receive benchmark\n";

    foreach (@execs) {
        my $exec = $_;

        unless(-e "./$lib/$exec") {
            die "The executable ./$lib/$exec has not been created! \n";
        }

        foreach ((1 .. $threads)) {
            my $thread = $_;
            foreach (@fps_vals) {
                print "Starting to benchmark receiving at $_ fps\n";
                my $logname = "recv_results_$thread" . "threads_$_". "fps_$iter" . "iter_$exec";

                my $result_file = "$lib/results/$logname";

                # remove result file if it exists
                if(-e "./$result_file") {
                    system("rm ./$result_file");
                }

                for ((1 .. $iter)) {
                    print "Starting to benchmark round $_\n";
                    $socket->send("start"); # I believe this is used to avoid firewall from blocking traffic
                    # please note that the local address for receiver is raddr
                    system ("(time ./$lib/receiver $raddr $port $saddr $port $thread $format $srtp) 2>> $result_file");
                }
            }
        }
    }

    print "Receive benchmark finished\n";
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
}

sub lat_send {
    print "Latency send benchmark\n";
    my ($lib, $addr, $port) = @_;
    my ($socket, $remote, $data);

    $socket = mk_ssock($addr, $port);
    $remote = $socket->accept();

    for ((1 .. 100)) {
        $remote->recv($data, 16);
        system ("./$lib/latency_sender >> $lib/results/latencies 2>&1");
    }
    print "Latency send benchmark finished\n";
}

sub lat_recv {
    print "Latency receive benchmark\n";
    my ($lib, $addr, $port) = @_;
    my $socket = mk_rsock($addr, $port);

    for ((1 .. 100)) {
        $socket->send("start");
        system ("./$lib/latency_receiver 2>&1 >/dev/null");
        sleep 2;
    }
    print "Latency receive benchmark finished\n";
}

# TODO explain every parameter
sub print_help {
    print "usage (benchmark):\n  ./benchmark.pl \n"
    . "\t--lib <uvgrtp|ffmpeg|live555>\n"
    . "\t--role <send|recv>\n"
    . "\t--addr <server address>\n"
    . "\t--port <server port>\n"
    . "\t--threads <# of threads>\n"
    . "\t--start <start fps>\n"
    . "\t--end <end fps>\n\n";

    print "usage (latency):\n  ./benchmark.pl \n"
    . "\t--latency\n"
    . "\t--role <send|recv>\n"
    . "\t--addr <server address>\n"
    . "\t--port <server port>\n"
    . "\t--lib <uvgrtp|ffmpeg|live555>\n\n" and exit;
}

GetOptions(
    "library|lib|l=s"     => \(my $lib = ""),
    "role|r=s"            => \(my $role = ""),
    "sender_addr|saddr=s"   => \(my $saddr = ""),
    "receiver_addr|raddr=s" => \(my $raddr = ""),
    "port|p=i"            => \(my $port = 0),
    "iterations|iter|i=i" => \(my $iter = 10),
    "threads|t=i"         => \(my $threads = 1),
    "start|s=f"           => \(my $start = 0),
    "end|e=f"             => \(my $end = 0),
    "step=i"              => \(my $step = 0),
    "use-nc|use-netcat"   => \(my $nc = 0),
    "fps=s"               => \(my $fps = ""),
    "latency"             => \(my $lat = 0),
    "srtp"                => \(my $srtp = 0),
    "exec=s"              => \(my $exec = "default"),
    "format=s"            => \(my $format = "hevc"),
    "help"                => \(my $help = 0)
) or die "failed to parse command line!\n";

$port = $DEFAULT_PORT if !$port;
$saddr = $DEFAULT_ADDR if !$saddr;
$raddr = $DEFAULT_ADDR if !$raddr;

print_help() if ((!$start or !$end) and !$fps) and !$lat;

die "library not supported\n" if !grep (/$lib/, ("uvgrtp", "ffmpeg", "live555"));
die "format not supported\n"  if !grep (/$format/, ("hevc", "vvc", "h265", "h266"));


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
    if ($lat) {
        system "make $lib" . "_latency_sender";
        lat_send($lib, $raddr, $port);
    } else {
        if ($exec eq "default") {
            system "make $lib" . "_sender";
            $exec = "sender";
        }
        send_benchmark($lib, $saddr, $raddr, $port, $iter, $threads, $nc, $exec, $format, $srtp, @fps_vals);
    }
} elsif ($role eq "recv" or $role eq "receive" or $role eq "receiver") {
    if ($lat) {
        system "make $lib" . "_latency_receiver";
        lat_recv($lib, $saddr, $port);
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
