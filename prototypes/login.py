#!/usr/bin/python
#
# Demo login widget.  Looks ugly.
#
# Author: Max Kellermann <mk@cm4all.com>

import os
import cgi
from sys import stdout

form = cgi.FieldStorage()

if form.has_key('user'):
    # login form was submitted
    print "Status: 302 Found"
    print "Location: ;translate=" + form['user'].value
    print "Content-Type: text/plain"
    body = "redirect"
elif form.has_key('logout'):
    # user clicked the "logout" button
    print "Status: 302 Found"
    print "Location: ;translate="
    print "Content-Type: text/plain"
    body = "redirect"
else:
    user = os.getenv('HTTP_X_CM4ALL_BENG_USER')
    if user is None:
        # anonymous: display login form
        print "Content-Type: text/html"
        body = """<h3>Login</h3>
<form method='post' action=';proxy'>
Name: <input name='user'><br>
<input type='submit'>
</form>
"""
    else:
        # authenticated: display user info and logout button
        print "Content-Type: text/html"
        body = """<p>Welcome, <b>%s</b>!</p>
<p><a href=";translate=">logout</a></p>
""" % user

print "Content-Length: %u" % len(body)
print
stdout.write(body)
