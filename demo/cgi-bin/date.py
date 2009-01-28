#!/usr/bin/python

import datetime

today = datetime.date.today()

print "Content-Type: text/html"
print
print today.strftime('%x')
