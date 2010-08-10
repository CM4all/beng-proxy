#
# Generic client for the beng-proxy remote control protocol.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import socket
import struct
from beng_proxy.control.protocol import *

class Client:
    def __init__(self, host, port=5478):
        assert isinstance(host, str)
        assert isinstance(port, int)

        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.connect((host, port))

    def send(self, command, payload=None):
        assert isinstance(command, int)
        if payload is None: payload = ''
        assert isinstance(payload, str)

        length = len(payload)
        assert length <= 0xffff
        header = struct.pack('>IHH', control_magic, length, command)
        padding = ' ' * (3 - ((length - 1) & 0x3))
        self._socket.send(header + payload + padding)

    def send_tcache_invalidate(self, vary):
        payload = ''
        for command, value in vary.iteritems():
            assert isinstance(command, int)
            assert isinstance(value, str)

            length = len(value)
            assert length <= 0xffff
            payload += struct.pack('>HH', length, command)
            payload += value
            payload += ' ' * (3 - ((length - 1) & 0x3))

        self.send(CONTROL_TCACHE_INVALIDATE, payload)
