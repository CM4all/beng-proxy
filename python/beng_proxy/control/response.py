#
# Parsers for beng-proxy remote control responses.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import struct

class MalformedResponseError(Exception):
    pass

class Stats:
    def __init__(self, payload):
        if len(payload) < 48:
            raise MalformedResponseError()

        self.incoming_connections, self.outgoing_connections, \
        self.children, self.sessions, \
        self.http_requests, \
        self.translation_cache_size, \
        self.http_cache_size, \
        self.filter_cache_size = \
        struct.unpack('>IIIIQQQQ', payload[:56])
