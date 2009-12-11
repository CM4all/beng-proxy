#!/usr/bin/python

from sys import stdin, stdout
from os import getenv

data = ''
while True:
    p = stdin.read(4096)
    if len(p) == 0: break
    data += p

print "Content-Type: %s" % getenv('HTTP_CONTENT_TYPE', 'application/octet-stream')
print "Content-Length: %u" % len(data)
print
stdout.write(data)
