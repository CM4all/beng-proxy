#!/usr/bin/python

from os import getenv
from datetime import datetime, timedelta
from time import mktime
from email.Utils import formatdate

steps = ('Start', 'Weiter', 'Noch weiter', 'Ende')

path_info = getenv('PATH_INFO', '')
if len(path_info) >= 2 and path_info[0] == '/':
    step = int(path_info[1:])
else:
    step = 0

now = datetime.now()
expires = now + timedelta(days=1)
expires = mktime(expires.timetuple())

print "Content-Type: text/html"
print "Expires: %s" % formatdate(timeval=expires, localtime=False, usegmt=True)
print
print "<div style='border:1px solid black;'><table width=100% border><tr>"
for i in range(len(steps)):
    label = steps[i]
    if i == step:
        print "<td><b>%s</b></td>" % label
    else:
        print "<td><a c:base='widget' c:mode='focus' href='%d'>%s</a></td>" % (i, label)
print "</tr></table>"

if step == 0:
    print "Willkommen beim Wizard!"
elif step == 1:
    print "<c:widget type='demo_date2_http'/>"
elif step == 2:
    print "<c:widget type='demo_hello'/>"
elif step == 3:
    print "Ende."
else:
    print "Fehler."

print "</div>"
