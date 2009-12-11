#!/usr/bin/python

from sys import stdin, stdout

body = """<form method='post' c:base='widget' c:mode='focus' action='post.py'>
<input name='foo'><br>
<input type='submit'>
</form>
"""

has_body = False
while True:
    p = stdin.read(4096)
    if len(p) == 0: break
    body += p
    has_body = True

print "Content-Type: text/html"
print "Content-Length: %u" % len(body)
if not has_body:
    import time
    expires = time.gmtime(time.time() + 24 * 3600)
    print "Expires: %s" % time.strftime("%a, %d %b %Y %H:%M:%S GMT", expires)
print
stdout.write(body)
