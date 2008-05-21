#!/usr/bin/python

import datetime

print "Content-Type: text/html"
print
print datetime.datetime.now().strftime('%x')
