#!/usr/bin/ruby
require "fcgi"

FCGI.each {|request|
    out = request.out
    out.print "Content-Type: text/plain\r\n"
    out.print "\r\n"
    out.print "Hello world"
    request.finish
}
