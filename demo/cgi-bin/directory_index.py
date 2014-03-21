#!/usr/bin/python

from os import environ, listdir

print "Content-Type: text/plain"
print

for i in listdir(environ['DIRECTORY']):
    print i
