#!/usr/bin/python
#
# Demo for a "control" client.
#
# Author: Max Kellermann <mk@cm4all.com>

import sys
from beng_proxy.control.client import Client

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "HOST VERBOSE"

if len(sys.argv) != 3:
    usage()
    sys.exit(1)

host = sys.argv[1]
verbose = int(sys.argv[2])

client = Client(host, broadcast=True)
client.send_verbose(verbose)
