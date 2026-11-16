#!/usr/bin/perl -w
use strict;

print "Content-type: text/plain\n\n";

foreach my $key (sort keys(%ENV)) {
    print "$key=$ENV{$key}\n";
}
