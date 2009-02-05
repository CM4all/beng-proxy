#!/usr/bin/python
#
# This widget demonstrates the http-cache code: with the Vary response
# header, beng-proxy knows how to cache different responses for the
# same resource.
#
# author: Max Kellermann <mk@cm4all.com>

from os import getenv
from sys import stdout
from datetime import date, timedelta
from time import mktime
from email.Utils import formatdate

body = getenv('HTTP_USER_AGENT', '')

expires = date.today() + timedelta(days=7)
expires = mktime(expires.timetuple())

print "Content-Type: text/plain"
print "Content-Length: %u" % len(body)
print "Expires: %s" % formatdate(timeval=expires, localtime=False, usegmt=True)
print "Vary: User-Agent"
print
stdout.write(body)
