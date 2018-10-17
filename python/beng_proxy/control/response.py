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

        fmt = '>IIIIQQQQQQQQQQQQQ'
        expected_length = struct.calcsize(fmt)

        if len(payload) > expected_length:
            payload = payload[:expected_length]
        elif len(payload) < expected_length:
            payload += '\0' * (expected_length - len(payload))

        self.incoming_connections, self.outgoing_connections, \
        self.children, self.sessions, \
        self.http_requests, \
        self.translation_cache_size, \
        self.http_cache_size, \
        self.filter_cache_size, \
        self.translation_cache_brutto_size, \
        self.http_cache_brutto_size, \
        self.filter_cache_brutto_size, \
        self.nfs_cache_size, self.nfs_cache_brutto_size, \
        self.io_buffers_size, self.io_buffers_brutto_size, \
        self.http_traffic_received, self.http_traffic_sent = \
        struct.unpack(fmt, payload)
