#!/usr/bin/env perl

use warnings;
use strict;
use Getopt::Long;
use Cwd qw(realpath);

my $TOTAL_FRAMES_UVGRTP  = 602;
my $TOTAL_FRAMES_LIVE555 = 601;
my $TOTAL_FRAMES_FFMPEG  = 598;

# open the file, validate it and return file handle to caller
sub open_file {
    my ($path, $expect) = @_;
    my $lines  = 0;

    open(my $fh, '<', $path) or die "failed to open file: $path";
    $lines++ while (<$fh>);

    seek $fh, 0, 0;
    return $fh;
}

sub goodput {
    if    ($_[2] eq "mbit" or $_[2] eq "Mbit") { return  8 * ($_[0] / 1000)        / $_[1]; }
    elsif ($_[2] eq "mb"   or $_[2] eq "MB")   { return      ($_[0] / 1000)        / $_[1]; }
    else                    { return  8 * ($_[0] / 1000 / 1000) / $_[1]; }
}

sub convert_bytes_to_unit {
    if    ($_[1] eq "mbit" or $_[1] eq "Mbit") { return  8 * ($_[0] / 1000 / 1000)        ; }
    elsif ($_[1] eq "mb"   or $_[1] eq "MB")   { return      ($_[0] / 1000 / 1000)        ; }
    else                    { return  8 * ($_[0] / 1000 / 1000 / 1000) ; }
}

sub get_frame_count {
    return ($_[0] eq "uvgrtp") ? $TOTAL_FRAMES_UVGRTP :
           ($_[0] eq "ffmpeg") ? $TOTAL_FRAMES_FFMPEG : $TOTAL_FRAMES_LIVE555;
}

sub parse_send {
    my ($lib, $iter, $threads, $path, $unit, $filesize) = @_;

    my ($t_usr, $t_sys, $t_cpu, $t_total, $t_time);
    my ($t_sgp, $t_tgp, $fh);

    if ($lib eq "uvgrtp") {
        my $e  = ($iter * ($threads + 2));
        $fh = open_file($path, $e);
        return if not defined $fh;
    } else {
        open $fh, '<', $path or die "failed to open file\n";
    }

    # each iteration parses one benchmark run
    # and each benchmark run can have 1..N entries, one for each thread
    START: while (my $line = <$fh>) {
        my $rt_avg = 0;
        my $rb_avg = 0;

        next if index ($line, "kB") == -1 or index ($line, "MB") == -1;

        # for multiple threads there are two numbers:
        #  - single thread performance
        #     -> for each thread, calculate the speed at which the data was sent,
        #        sum all those together and divide by the number of threads
        #
        #  - total performance
        #     -> (amount of data * number of threads) / total time spent
        #
        for (my $i = 0; $i < $threads; $i++) {
            next START if grep /terminated|corrupt/, $line;
            my @nums = $line =~ /(\d+)/g;
            $rt_avg += $nums[3];
            $line = <$fh>;
        }
        $rt_avg /= $threads;

        next START if grep /terminated|corrupt/, $line;
        $line = <$fh> if grep /flush|Command/, $line;
        my ($usr, $sys, $total, $cpu) = ($line =~ m/(\d+\.\d+)user\s(\d+\.\d+)system\s0:(\d+.\d+)elapsed\s(\d+)%CPU/);

        # discard line about inputs, outputs and pagefaults
        $line = <$fh>;

        # update total
        $t_usr   += $usr;
        $t_sys   += $sys;
        $t_cpu   += $cpu;
        $t_total += $total;
        $t_sgp   += goodput($filesize, $rt_avg, $unit);
    }

    $t_sgp = $t_sgp / $iter;
    $t_tgp = ($threads > 1) ? goodput($filesize * $threads, $t_total / $iter, $unit) : $t_sgp;

    close $fh;
    return ($path, $t_usr / $iter, $t_sys / $iter, $t_cpu / $iter / 100.0, $t_total / $iter, $t_sgp, $t_tgp);
}

sub parse_recv {
    my ($lib, $iter, $threads, $path, $unit, $filesize) = @_;
    my ($t_usr, $t_sys, $t_cpu, $t_total, $tb_avg, $tf_avg, $tt_avg, $fh);
    my $tf = get_frame_count($lib);

    if ($lib eq "uvgrtp") {
        my $e  = ($iter * ($threads + 2));
        $fh = open_file($path, $e);
    } else {
        open $fh, '<', $path or die "failed to open file $path\n";
    }

    # each iteration parses one benchmark run
    while (my $line = <$fh>) {
        my ($a_f, $a_b, $a_t) = (0) x 3;

        # make sure this is a line produced by the benchmarking script before proceeding
        if ($lib eq "ffmpeg") {
            my @nums = $line =~ /(\d+)/g;
            next if $#nums != 2 or grep /jitter/, $line;
        }

        # calculate avg bytes/frames/time
        for (my $i = 0; $i < $threads; $i++) {
            my @nums = $line =~ /(\d+)/g;

            $a_b += $nums[0];
            $a_f += $nums[1];
            $a_t += $nums[2];

            $line = <$fh>;
        }

        $tf_avg += ($a_f / $threads);
        $tb_avg += ($a_b / $threads);
        $tt_avg += ($a_t / $threads);

        # $line = <$fh> if grep /Command/, $line;

        my ($usr, $sys, $total, $cpu) = ($line =~ m/(\d+\.\d+)user\s(\d+\.\d+)system\s0:(\d+.\d+)elapsed\s(\d+)%CPU/);

        if (!defined($usr) or !defined($sys)) {
            die "$line, $path";
        }

        # discard line about inputs, outputs and pagefaults
        $line = <$fh>;

        # update total
        $t_usr   += $usr;
        $t_sys   += $sys;
        $t_cpu   += $cpu;
        $t_total += $total;
    }

    my $bytes      = (($tb_avg  / $iter) / $filesize);
    my $frame_loss = 1.0 - (($tf_avg  / $iter) / $tf);
    my $gp         = goodput(($filesize * ($bytes), ($tt_avg  / $iter)), $unit);

    close $fh;
    return ($path, $t_usr / $iter, $t_sys / $iter, $t_cpu / $iter / 100.0, $t_total / $iter, $frame_loss, $bytes, $gp);
}

sub print_recv {
    my ($path, $usr, $sys, $cpu, $total, $a_f, $a_b, $a_t) = parse_recv(@_);

    if (defined $path) {
        print "$path: \n";
        print "\tuser:         $usr  \n";
        print "\tsystem:       $sys  \n";
        print "\tcpu:          $cpu  \n";
        print "\ttotal:        $total\n";
        print "\tavg frames:   $a_f\n";
        print "\tavg bytes:    $a_b\n";
        print "\trecv goodput: $a_t\n";
    }
}

sub print_send {
    my ($path, $usr, $sys, $cpu, $total, $sgp, $tgp) = parse_send(@_);

    if (defined $path) {
        print "$path: \n";
        print "\tuser:            $usr\n";
        print "\tsystem:          $sys\n";
        print "\tcpu:             $cpu\n";
        print "\ttotal:           $total\n";
        print "\tgoodput, single: $sgp\n";
        print "\tgoodput, total:  $tgp\n";
    }
}

sub parse_csv {
    my ($lib, $iter, $path, $unit, $filesize) = @_;
    my ($threads, $fps, $ofps, $fiter, %result_ids) = (0) x 4;
    opendir my $dir, realpath($path);
    
    my $recv_present = 0;
    my $send_present = 0;
    
    foreach my $filename (grep /(recv|send)/, readdir $dir) {
        ($threads, $ofps, $fiter) = ($filename =~ /(\d+)threads_(\d+)fps_(\d+)rounds/g);
        $iter = $fiter if $fiter;
        print "unable to determine iter, skipping file $filename\n" and next if !$iter;
        $fps = sprintf("%05d", $ofps);
        my @values;
        
        # calculate send datarate for this fps
        my $data_rate = $ofps*$filesize/get_frame_count($lib);
        
        $data_rate = convert_bytes_to_unit($data_rate, $unit);

        if (grep /recv/, $filename) {
            $recv_present += 1;
            @values = parse_recv($lib, $iter, $threads, realpath($path) . "/" . $filename, $unit, $filesize);
            shift @values; # removes first value?

            if (not exists $result_ids{"$threads $fps"}) {
                $result_ids{"$threads $fps"} = join(" ", @values);
            } else {
                $result_ids{"$threads $fps"} = join(" ", @values) . " " . $result_ids{"$threads $fps"};
            }

        } else {
            $send_present += 1;
            @values = parse_send($lib, $iter, $threads, realpath($path) . "/" . $filename, $unit, $filesize);
            shift @values; # removes first value?

            if (not exists $result_ids{"$threads $fps"}) {
                $result_ids{"$threads $fps"} = join(" ", @values) . " $ofps" . " $data_rate";
            } else {
                $result_ids{"$threads $fps"} = $result_ids{"$threads $fps"} . " " . join(" ", @values) . " $ofps" . " $data_rate";
            }
        }
    }
    
    die "Different amounts of send and recv results!" if $recv_present ne $send_present;

    my $previous_threads = 0;
    open my $output_file, '>', "$lib.csv" or die "failed to open file: $lib.csv";

    my $frame_count = get_frame_count($lib);
    my $filesize_in_units = convert_bytes_to_unit($filesize, $unit);
    
    # print the thread number on first line on the file
    print $output_file "File size ($unit); $filesize_in_units; \n";
    print $output_file "Frame count; $frame_count;\n\n";
    
    # create empty variables
    my (@recv_usr, @recv_sys, @recv_cpu, @recv_total, @recv_frame, @recv_bytes, @recv_goodput) = () x 7;
    my (@send_usr, @send_sys, @send_cpu, @send_total, @send_thread_goodput, @send_total_goodput, @send_fps, @datarate) = () x 8;
    
    foreach my $key (sort(keys %result_ids)) {
        my $threads_of_result = (split " ", $key)[0];
        
        if ($threads_of_result != $previous_threads){
            
            # print run results expect for one thread results which are printed later
            if ($threads_of_result ne 1) {
                print $output_file "fps;"                . join(";", @send_fps)  . "\n";
                print $output_file "datarate ($unit);"   . join(";", @datarate)  . "\n\n";
                
                print $output_file "send user time (s);"     . join(";", @send_usr)  . "\n";
                print $output_file "send system time (s);"   . join(";", @send_sys)  . "\n";
                print $output_file "send elapsed time (s);"  . join(";", @send_total)  . "\n";
                print $output_file "send CPU usage (%);"     . join(";", @send_cpu)  . "\n";
                print $output_file "send goodput ($unit);"       . join(";", @send_thread_goodput) . "\n";
                print $output_file "total send goodput ($unit);" . join(";", @send_total_goodput) . "\n\n";
                
                print $output_file "recv user time (s);"     . join(";", @recv_usr)  . "\n";
                print $output_file "recv system time (s);"   . join(";", @recv_sys)  . "\n";
                print $output_file "recv elapsed time (s);"  . join(";", @recv_total)  . "\n";
                print $output_file "recv CPU usage (%);"     . join(";", @recv_cpu)  . "\n";
                print $output_file "frame loss (%);"          . join(";", @recv_frame)  . "\n";
                print $output_file "bytes received (%);"     . join(";", @recv_bytes)  . "\n";
                print $output_file "recv goodput ($unit);"       . join(";", @recv_goodput)  . "\n\n";
            }
            
            # print the thread number on first line on the file
            print $output_file "$threads_of_result threads;\n";
            $previous_threads = $threads_of_result;
            
            # reset variable values
            (@recv_usr, @recv_sys, @recv_cpu, @recv_total, @recv_frame, @recv_bytes, @recv_goodput) = () x 7;
            (@send_usr, @send_sys, @send_cpu, @send_total, @send_thread_goodput, @send_total_goodput, @send_fps, @datarate) = () x 8;
        }
        
        # set the values for printing
        my @comp = split " ", $result_ids{$key};
        push @recv_usr,           $comp[0];  push @recv_sys,   $comp[1];  push @recv_cpu,            $comp[2];
        push @recv_total,         $comp[3];  push @recv_frame, $comp[4];  push @recv_bytes,          $comp[5];
        push @recv_goodput,       $comp[6];  push @send_usr,   $comp[7];  push @send_sys,            $comp[8];
        push @send_cpu,           $comp[9];  push @send_total, $comp[10]; push @send_thread_goodput, $comp[11];
        push @send_total_goodput, $comp[12]; push @send_fps,   $comp[13]; push @datarate,            $comp[14];
    }
    
    # print values for one thread run
    print $output_file "fps;"                . join(";", @send_fps)  . "\n";
    print $output_file "datarate ($unit);"           . join(";", @datarate)  . "\n\n";
    
    print $output_file "send user time (s);"     . join(";", @send_usr)  . "\n";
    print $output_file "send system time (s);"   . join(";", @send_sys)  . "\n";
    print $output_file "send elapsed time (s);"  . join(";", @send_total)  . "\n";
    print $output_file "send CPU usage (%);"     . join(";", @send_cpu)  . "\n";
    print $output_file "send goodput ($unit);"       . join(";", @send_thread_goodput) . "\n";
    print $output_file "total send goodput ($unit);" . join(";", @send_total_goodput) . "\n\n";
    
    print $output_file "recv user time (s);"     . join(";", @recv_usr)  . "\n";
    print $output_file "recv system time (s);"   . join(";", @recv_sys)  . "\n";
    print $output_file "recv elapsed time (s);" . join(";", @recv_total)  . "\n";
    print $output_file "recv CPU usage (%);"     . join(";", @recv_cpu)  . "\n";
    print $output_file "frame loss (%);"         . join(";", @recv_frame)  . "\n";
    print $output_file "bytes received (%);"     . join(";", @recv_bytes)  . "\n";
    print $output_file "recv goodput ($unit);"       . join(";", @recv_goodput)  . "\n\n";

    close $output_file;
}

sub parse {
    my ($lib, $iter, $path, $pkt_loss, $frame_loss, $type, $unit, $filesize) = @_;
    my ($tgp, $tgp_k, $sgp, $sgp_k, $threads, $fps, $fiter, %a) = (0) x 7;
    opendir my $dir, realpath($path);

    foreach my $fh (grep /recv/, readdir $dir) {
        ($threads, $fps, $fiter) = ($fh =~ /(\d+)threads_(\d+)fps_(\d+)rounds/g);
        $iter = $fiter if $fiter;
        print "unable to determine iter, skipping file $fh\n" and next if !$iter;

        my @values = parse_recv($lib, $iter, $threads, realpath($path) . "/" . $fh, $unit, $filesize);

        if (100.0 - $values[5] <= $frame_loss and 100.0 - $values[6] <= $pkt_loss) {
            $a{"$threads $fps"} = $path;
        }
    }

    rewinddir $dir;

    foreach my $fh (grep /send/, readdir $dir) {
        ($threads, $fps, $fiter) = ($fh =~ /(\d+)threads_(\d+)fps_(\d+)rounds/g);
        $iter = $fiter if $fiter;
        print "unable to determine iter, skipping file $fh\n" and next if !$iter;

        my @values = parse_send($lib, $iter, $threads, realpath($path) . "/" . $fh, $unit, $filesize);

        if (exists $a{"$threads $fps"}) {
            if ($type eq "best") {
                if ($values[5] > $sgp) {
                    $sgp   = $values[5];
                    $sgp_k = $fh;
                }

                if ($values[6] > $tgp) {
                    $tgp = $values[6];
                    $tgp_k = $fh;
                }
            } else {
                print "$fh: $values[5] $values[6]\n" if exists $a{"$threads $fps"};
            }
        }
    }

    closedir $dir;
    exit if $type eq "all";

    if ($sgp_k) {
        print "best goodput, single thread: $sgp_k\n";
        ($threads, $fps) = ($sgp_k =~ /(\d+)threads_(\d+)/g);
        print_send($lib, $iter, $threads, realpath($path) . "/" . $sgp_k, $unit, $filesize);
    } else {
        print "nothing found for single best goodput\n";
    }

    if ($tgp_k) {
        print "\nbest goodput, total: $tgp_k\n";
        ($threads, $fps) = ($tgp_k =~ /(\d+)threads_(\d+)/g);
        print_send($lib, $iter, $threads, realpath($path) . "/" . $tgp_k, $unit, $filesize);
    } else {
        print "nothing found for total best goodput\n";
    }
}

sub parse_latency {
    my ($lib, $path, $unit) = @_;
    my ($frames, $avg, $intra, $inter, $cnt) = (0) x 5;
    my $frame_avg = 0;

    open my $fh, '<', $path or die "failed to open file $path\n";
    
    my $rounds = 0;
    # each iteration parses one benchmark run
    while (my $line = <$fh>) {
        my @nums = ($line =~ m/(\d+).*intra\s(\d+\.\d+).*inter\s(\d+\.\d+).*avg\s(\d+\.\d+)/);

        if ($nums[0] == 598) {
            $frame_avg++;
        }

        $frames += $nums[0];
        $intra  += $nums[1];
        $inter  += $nums[2];
        $avg    += $nums[3];
        $cnt    += 1;
        $rounds += 1;
    }

    $intra  /= $cnt;
    $inter  /= $cnt;
    $avg    /= $frame_avg;
    $frames = 100*$frames/(598*$rounds);

    print "Completed: $frames%, intra $intra ms, inter $inter ms, avg $avg ms\n";
}

sub print_help {
    print "usage (one file, send/recv):\n  ./parse.pl \n"
    . "\t--lib <uvgrtp|ffmpeg|live555>\n"
    . "\t--role <send|recv>\n"
    . "\t--unit <mb|mbit|gbit> (defaults to mb)\n"
    . "\t--path <path to log file>\n"
    . "\t--iter <# of iterations>)\n"
    . "\t--filesize <size of the test file in bytes>\n"
    . "\t--threads <# of threads used in the benchmark> (defaults to 1)\n\n";

    print "usage (latency):\n  ./parse.pl \n"
    . "\t--unit <mb|mbit|gbit> (defaults to mb)\n"
    . "\t--path <path to log file>\n"
    . "\t--parse latency\n\n";

    print "usage (directory):\n  ./parse.pl \n"
    . "\t--parse <best|all|csv>\n"
    . "\t--lib <uvgrtp|ffmpeg|live555>\n"
    . "\t--iter <# of iterations> (not needed if correct file format)\n"
    . "\t--unit <mb|mbit|gbit> (defaults to mb)\n"
    . "\t--filesize <size of the test file in bytes> (use ls -l to get this, mandatory)\n"
    . "\t--packet-loss <allowed percentage of dropped packets> (optional)\n"
    . "\t--frame-loss <allowed percentage of dropped frames> (optional)\n"
    . "\t--path <path to folder with send and recv output files>\n" and exit;
}

GetOptions(
    "library|lib|l=s"              => \(my $lib = ""),
    "role|r=s"                     => \(my $role = ""),
    "path|dir|directory|p=s"       => \(my $path = ""),
    "threadst|threads|=i"          => \(my $threads = 0),
    "iter|iterations|rounds|i=i"   => \(my $iter = 0),
    "fsize|size|filesize|fs|i=i"   => \(my $filesize = 0),
    "parse|type|s=s"       => \(my $parse = ""),
    "packet-loss|pl|p=f" => \(my $pkt_loss = 100.0),
    "frame-loss|fl|f=f"  => \(my $frame_loss = 100.0),
    "unit=s"          => \(my $unit = "MB"),
    "help"            => \(my $help = 0)
) or die "failed to parse command line!\n";

$lib     = $1 if (!$lib     and $path =~ m/.*(uvgrtp|ffmpeg|live555).*/i);
$role    = $1 if (!$role    and $path =~ m/.*(recv|send).*/i);
$threads = $1 if (!$threads and $path =~ m/.*_(\d+)threads.*/i);
$iter    = $1 if (!$iter    and $path =~ m/.*_(\d+)rounds.*/i);

print_help() if $help or (!$lib and $parse ne "latency");
print_help() if !$iter and !$parse;
print_help() if !$parse and (!$role or !$threads);
print_help() if !grep /$unit/, ("mb", "MB", "mbit", "Mbit", "Gbit", "gbit");

die "library not implemented\n" if !grep (/$lib/, ("uvgrtp", "ffmpeg", "live555"));

die "please specify test file size from ls -l command with --filesize" if !$filesize and $parse ne "latency";

if ($parse eq "best" or $parse eq "all") {
    parse($lib, $iter, $path, $pkt_loss, $frame_loss, $parse, $unit, $filesize);
} elsif ($parse eq "csv") {
    parse_csv($lib, $iter, $path, $unit, $filesize);
} elsif ($parse eq "latency") {
    parse_latency($lib, $path, $unit);
} elsif ($role eq "send") {
    print_send($lib, $iter, $threads, $path, $unit, $filesize);
} elsif ($role eq "recv") {
    print_recv($lib, $iter, $threads, $path, $unit, $filesize);
} else {
    die "unknown option!\n";
}
