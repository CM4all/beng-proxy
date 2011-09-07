#!/usr/bin/python
# Author: Max Kellermann <mk@cm4all.com>

import sys
import struct
from beng_proxy.control.client import Client
from beng_proxy.translation.protocol import *

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "HOST NODE PORT"

if len(sys.argv) != 4:
    usage()
    sys.exit(1)

host, node, port = sys.argv[1:4]
port = int(port)

client = Client(host)
client.send_fade_node(node, port)
