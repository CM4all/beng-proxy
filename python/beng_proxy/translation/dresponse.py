#
# A builder for a translation response.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import struct
from twisted.names.client import getHostByName
from twisted.internet import defer
from protocol import *
from serialize import packet_header
from response import Response

class DeferredResponse(Response):
    """Generator for a translation response.  This derived class is
    non-blocking, i.e. it uses Twisted's asynchronous DNS resolver
    instead of the synchronous function gethostbyname()."""

    def _resolve_success(self, result, d, port):
        if port: result += ':' + port
        self.packet(TRANSLATE_ADDRESS_STRING, result)
        d.callback(self)

    def proxy(self, uri, addresses=None):
        """Non-blocking version of Response.proxy() - returns a
        Deferred if the resolver is used."""
        assert uri[0] != '/' or addresses is None or len(addresses) == 0
        assert addresses is None or hasattr(addresses, '__iter__')

        self.packet(TRANSLATE_PROXY, uri)

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            from urlparse import urlparse

            d = defer.Deferred()

            host, port = (urlparse(uri)[1].split(':', 1) + [None])[0:2]
            e = getHostByName(host)
            e.addCallback(self._resolve_success, d, port)
            e.addErrback(d.errback)

            return d

    def ajp(self, uri, addresses=None):
        """Non-blocking version of Response.proxy() - returns a
        Deferred if the resolver is used."""
        assert uri[0] != '/' or addresses is None or len(addresses) == 0
        assert addresses is None or hasattr(addresses, '__iter__')

        self.packet(TRANSLATE_AJP, uri)

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            from urlparse import urlparse

            d = defer.Deferred()

            host, port = (addresses.split(':', 1) + [None])[:2]
            e = getHostByName(host)
            e.addCallback(self._resolve_success, d, port)
            e.addErrback(d.errback)

            return d
