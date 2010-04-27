#
# A builder for a translation response.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import struct
from protocol import *
from serialize import packet_header

class Response:
    """Generator for a translation response.  The BEGIN and END
    packets are generated automatically.  When you are done with the
    response, call finish().  This method returns the full response
    (all serialized packets) as a string."""

    def __init__(self):
        self._data = packet_header(TRANSLATE_BEGIN)

    def finish(self):
        """Finish the response, and return it as a string."""
        self._data += packet_header(TRANSLATE_END)
        return self._data

    def packet(self, command, payload = ''):
        """Append a packet."""
        assert isinstance(payload, str)
        self._data += packet_header(command, len(payload))
        self._data += payload
        return self

    def status(self, status):
        """Append a STATUS packet."""
        assert status >= 200 and status < 600
        return self.packet(TRANSLATE_STATUS, struct.pack('H', status))

    def view(self, name):
        """Append a VIEW packet."""
        assert isinstance(name, str)
        assert len(name) > 0
        return self.packet(TRANSLATE_VIEW, name)

    def process(self, container=False):
        """Append a PROCESS packet, and also a CONTAINER packet if the
        'container' argument is true."""
        self.packet(TRANSLATE_PROCESS)
        if container:
            self.packet(TRANSLATE_CONTAINER)
        return self

    def proxy(self, uri, addresses=None):
        """Generate a PROXY packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""
        assert uri[0] != '/' or len(addresses) == 0
        assert addresses is None or hasattr(addresses, '__iter__')

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            from urlparse import urlparse
            from socket import gethostbyname

            host, port = (urlparse(uri)[1].split(':', 1) + [None])[0:2]
            address = gethostbyname(host)
            if port: address += ':' + port
            addresses = (address,)

        self.packet(TRANSLATE_PROXY, uri)
        for address in addresses:
            self.packet(TRANSLATE_ADDRESS_STRING, address)
        return self

    def ajp(self, uri, addresses):
        """Generate an AJP packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""

        assert isinstance(addresses, str) or hasattr(address, '__iter__')

        if isinstance(addresses, str):
            from socket import gethostbyname
            host, port = (addresses.split(':', 1) + [None])[:2]
            address = gethostbyname(host)
            if port: address += ':' + port
            addresses = (address,)

        self.packet(TRANSLATE_AJP, uri)
        for address in addresses:
            self.packet(TRANSLATE_ADDRESS_STRING, address)
        return self

    def vary(self, *args):
        """Send a VARY packet.  All arguments are packet ids which are
        put into the VARY packet payload."""

        assert len(args) > 0
        payload = ''.join(map(lambda x: struct.pack('H', x), args))
        return self.packet(TRANSLATE_VARY, payload)

    def invalidate(self, *args):
        """Send a INVALIDATE packet.  All arguments are packet ids
        which are put into the INVALIDATE packet payload."""

        assert len(args) > 0
        payload = ''.join(map(lambda x: struct.pack('H', x), args))
        return self.packet(TRANSLATE_INVALIDATE, payload)

    def pipe(self, path, *args):
        """Send a PIPE packet.  You may pass additional arguments
        which are sent as APPEND packets."""

        assert isinstance(path, str)
        assert len(path) > 0
        self.packet(TRANSLATE_PIPE, path)
        for arg in args:
            self.packet(TRANSLATE_APPEND, arg)
        return self

    def path(self, path):
        assert isinstance(path, str)
        assert len(path) > 0
        assert path[0] == '/'
        return self.packet(TRANSLATE_PATH, path)

    def gzipped(self, path):
        assert isinstance(path, str)
        assert len(path) > 0
        assert path[0] == '/'
        return self.packet(TRANSLATE_GZIPPED, path)

    def pair(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_PAIR, name + '=' + value)

    def content_type(self, content_type):
        assert isinstance(content_type, str)
        assert content_type.find('/') > 0
        return self.packet(TRANSLATE_CONTENT_TYPE, content_type)

    def delegate(self, helper):
        assert isinstance(helper, str)
        return self.packet(TRANSLATE_DELEGATE, helper)

    def delegated_path(self, helper, path):
        self.path(path)
        self.delegate(helper)
        return self

    def header_forward(self, command, *args):
        payload = ''
        for x in args:
            assert isinstance(x, tuple)
            assert len(x) == 2
            assert isinstance(x[0], int)
            assert isinstance(x[1], int)

            payload += struct.pack('hBx', *x)
        return self.packet(command, payload)

    def request_header_forward(self, *args):
        return self.header_forward(TRANSLATE_REQUEST_HEADER_FORWARD,
                                   *args)

    def response_header_forward(self, *args):
        return self.header_forward(TRANSLATE_RESPONSE_HEADER_FORWARD,
                                   *args)

    def header(self, name, value):
        return self.packet(TRANSLATE_HEADER, name + ':' + value)
