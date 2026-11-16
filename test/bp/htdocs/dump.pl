#!/usr/bin/perl -w
use strict;

print "Content-type: text/plain\n\n";

foreach my $key (qw(REQUEST_URI SCRIPT_NAME PATH_INFO QUERY_STRING)) {
    print "$key=$ENV{$key} " if exists $ENV{$key};
}
