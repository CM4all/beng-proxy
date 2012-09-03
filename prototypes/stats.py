#!/usr/bin/python
# Author: Max Kellermann <mk@cm4all.com>

import sys
import struct
from beng_proxy.control.client import Client
from beng_proxy.control.protocol import *
from beng_proxy.control.response import *

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "HOST"

if len(sys.argv) != 2:
    usage()
    sys.exit(1)

host = sys.argv[1]

client = Client(host)
client.send_stats()

for command, payload in client.receive():
    if command == CONTROL_STATS:
        stats = Stats(payload)
        print "incoming_connections", stats.incoming_connections
        print "outgoing_connections", stats.outgoing_connections
        print "children", stats.children
        print "sessions", stats.sessions
        print "http_requests", stats.http_requests
        print "translation_cache_size", stats.translation_cache_size
        print "http_cache_size", stats.http_cache_size
        print "filter_cache_size", stats.filter_cache_size

        sys.exit(0)

print >>sys.stderr, "Server did not respond"
sys.exit(1)
