#!/usr/bin/python
# Author: Max Kellermann <mk@cm4all.com>

import sys
import struct
from beng_proxy.control.client import Client

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "HOST [TAG]"

if len(sys.argv) < 2 or len(sys.argv) > 3:
    usage()
    sys.exit(1)

host = sys.argv[1]
tag = None
if len(sys.argv) > 2:
    tag = sys.argv[2]

client = Client(host)
client.send_fade_children(tag)
