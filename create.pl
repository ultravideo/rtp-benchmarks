#!/usr/bin/env perl

use warnings;
use strict;
use Getopt::Long;

$| = 1; # autoflush

sub print_help {
    print "usage (create):\n"  
    . "./create.pl \n"
    . "\t--input      <filename of YUV420 file>\n"
    . "\t--res        <{width}x{height}>\n"
    . "\t--qp         <qp value>\n"
    . "\t--fps        <file framerate value>\n"
    . "\t--intra      <intra period>\n"
    . "\t--preset     <encoding preset>\n"
}

GetOptions(
    "input|file|filename|i=s" => \(my $filename = ""),
    "resolution|res=s"        => \(my $resolution = "3840x2160"),
    "quantization|qp=i"       => \(my $qp = 27),
    "framerate|fps=i"         => \(my $fps = 30),
    "intra-period|intra=i"    => \(my $period = 64),
    "preset|pre=s"            => \(my $preset = "ultrafast"),
    "help"                    => \(my $help = 0)
) or die "failed to parse command line!\n";

print_help() if $help or !$filename;

# check that parameters make sense
die "" if $help;
die "please specify input file with --input" if !$filename;
die "invalid preset" if !grep (/$preset/, ("ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", "placebo"));

die "check resolution format, for example: --res 3840x2160\n" if $resolution !~ /([\d]+)x([\d]+)/;

# get resolution components
my $width = $1;
my $height = $2;

# build creation program
system "make test_file_creation"; 

 # run file creation
my $exit_code = system ("./test_file_creation $filename $width $height $qp $fps $period $preset");

if($exit_code!=0)
{
  die "Failed to run file creator.\n";
}
