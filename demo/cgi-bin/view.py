#!/usr/bin/python

from os import environ

path_info = environ['PATH_INFO']
if len(path_info) > 1:
    print "X-CM4all-View: " + path_info[1:]

print "Content-Type: text/plain"
print
print "Hello world!"
