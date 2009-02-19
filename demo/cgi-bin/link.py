#!/usr/bin/python

from os import getenv
from datetime import datetime, timedelta
from time import mktime
from email.Utils import formatdate

now = datetime.now()
expires = now + timedelta(days=1)
expires = mktime(expires.timetuple())

print "Content-Type: text/html"
print "Expires: %s" % formatdate(timeval=expires, localtime=False, usegmt=True)
print

path_info = getenv('PATH_INFO', '')
if path_info == '/test':
    print '<a c:base="widget" c:mode="focus" href=".">Zur&uuml;ck</a>'
else:
    print '<a href="test">Default</a><br>'
    print '<a c:base="widget" href="test">base=widget</a><br>'
    print '<a c:base="widget" c:mode="focus" href="test">base=widget, mode=focus</a><br>'
    print '<a c:base="widget" c:mode="partial" href="test">base=widget, mode=partial</a><br>'
