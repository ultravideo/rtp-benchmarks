#!/usr/bin/env perl

use warnings;
use strict;
use Getopt::Long;

$| = 1; # autoflush

sub print_help {
    print "usage (network):\n"  
    . "./network.pl \n"
    . "\t--role       <s/c> (mandatory)\n"
    . "\t--address    <send or receive address> (mandatory for sender)\n"
    . "\t--port       <qp value>\n"
    . "\t--psize      <size of tested packets>\n"
}

GetOptions(
    "role|r=s"              => \(my $role = "server"),
    "address|addr|a=s"      => \(my $address = "0.0.0.0"),
    "port|p=i"              => \(my $port = 8888),
    "packetsize|psize|p=i"  => \(my $size = 1458),
    "help|h"                => \(my $help = 0)
) or die "failed to parse command line!\n";

print_help() if $help or !$role;

die "" if $help;

# check that parameters make sense
die "invalid role" if !grep (/$role/, ("server", "s", "client", "c", "a", "send", "sender", "receive", "receiver", "recv"));
die "zero size not allowed" if ($size eq 0);

# build network program
system "g++ ./udperf.cc -o udperf"; 

if (grep (/$role/, ("server", "s", "receive", "receiver", "recv"))) {
    $role = "-s";
} else {
    die "Sender needs an address to send to" if !$address;
    $role = "-c";
}

# run file creation
my $exit_code = system ("./udperf $role $address -p $port -i $size");

if($exit_code!=0)
{
  die "Failed to run network tester.\n";
}

