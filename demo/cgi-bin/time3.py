#!/usr/bin/python

import sys
from datetime import datetime, timedelta
from time import mktime
from email.Utils import formatdate

now = datetime.now().replace(second=0)
soon = now + timedelta(minutes=1)
soon = mktime(soon.timetuple())
modified = mktime(now.timetuple())

x = now.strftime('%H:%M')

print "Content-Type: text/html"
print "Content-Length: %u" % len(x)
print "Last-Modified:", formatdate(timeval=modified, localtime=False, usegmt=True)
print "Expires:", formatdate(timeval=soon, localtime=False, usegmt=True)
print
sys.stdout.write(x)
