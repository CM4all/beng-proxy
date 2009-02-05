#!/usr/bin/python

from datetime import date, timedelta
from time import mktime
from email.Utils import formatdate

today = date.today()
midnight = today + timedelta(days=1)
midnight = mktime(midnight.timetuple())
modified = mktime(today.timetuple())

print "Content-Type: text/html"
print "Last-Modified:", formatdate(timeval=modified, localtime=False, usegmt=True)
print "Expires:", formatdate(timeval=midnight, localtime=False, usegmt=True)
print
print today.strftime('%x')
