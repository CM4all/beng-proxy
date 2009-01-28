#!/usr/bin/python

from datetime import date

today = date.today()

print "Content-Type: text/html"
print
print today.strftime('%x')
