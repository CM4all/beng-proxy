#!/usr/bin/python

from cgi import FieldStorage
from os import stat
from stat import ST_MTIME
from datetime import datetime, timedelta
from time import mktime
from email.Utils import formatdate
from pygments.lexers import get_lexer_for_filename
from pygments.formatters.html import HtmlFormatter
from pygments import highlight
from sys import stdout

form = FieldStorage()
path = form["path"].value
if path.find('/') >= 0: raise "Unauthorized path"

mtime = stat(path)[ST_MTIME]
mtime = mktime(datetime.utcfromtimestamp(mtime).timetuple())
expires = mktime((datetime.now() + timedelta(minutes=5)).timetuple())

code = file(path).read()
lexer = get_lexer_for_filename(path)
formatter = HtmlFormatter()

print "Content-Type: text/html"
print "Last-Modified:", formatdate(timeval=mtime, localtime=False, usegmt=True)
print "Expires:", formatdate(timeval=expires, localtime=False, usegmt=True)
print
highlight(code, lexer, formatter, outfile=stdout)
