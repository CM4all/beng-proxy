#!/usr/bin/python

from datetime import datetime, timedelta
from time import mktime
from email.Utils import formatdate

now = datetime.now().replace(second=0)
soon = now + timedelta(minutes=1)
soon = mktime(soon.timetuple())
modified = mktime(now.timetuple())

print "Content-Type: text/html"
print "Last-Modified:", formatdate(timeval=modified, localtime=False, usegmt=True)
print "Expires:", formatdate(timeval=soon, localtime=False, usegmt=True)
print
print now.strftime('%H:%M')
