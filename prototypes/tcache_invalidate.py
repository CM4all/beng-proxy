#!/usr/bin/python
#
# Demo for a "control" client.
#
# Author: Max Kellermann <mk@cm4all.com>

import sys
import struct
from beng_proxy.control.client import Client
from beng_proxy.translation.protocol import *

attributes = {
    'URI': TRANSLATE_URI,
    'SESSION': TRANSLATE_URI,
    #'LOCAL_ADDRESS': XXX
    'REMOTE_HOST': TRANSLATE_REMOTE_HOST,
    'HOST': TRANSLATE_HOST,
    'LANGUAGE': TRANSLATE_LANGUAGE,
    'USER_AGENT': TRANSLATE_USER_AGENT,
    'QUERY_STRING': TRANSLATE_QUERY_STRING,
}

def usage():
    print >>sys.stderr, "Usage:", sys.argv[0], "HOST field1=value1 [...]"
    print >>sys.stderr, "Supported fields:"
    for x in attributes.iterkeys():
        print >>sys.stderr, x

if len(sys.argv) < 3:
    usage()
    sys.exit(1)

host = sys.argv[1]

vary = {}
for arg in sys.argv[2:]:
    s = arg.split('=', 1)
    if len(s) != 2 or s[0] not in attributes:
        usage()
        sys.exit(1)

    vary[attributes[s[0]]] = s[1]

client = Client(host)
client.send_tcache_invalidate(vary)
