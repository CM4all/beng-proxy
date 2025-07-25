#
# A builder for a translation response.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

import struct
from typing import Collection
from urllib.parse import urlparse

from twisted.names.client import getHostByName
from twisted.internet import defer
from .protocol import *
from .serialize import packet_header
from .response import Response

class DeferredResponse(Response):
    """Generator for a translation response.  This derived class is
    non-blocking, i.e. it uses Twisted's asynchronous DNS resolver
    instead of the synchronous function gethostbyname()."""

    def _resolve_success(self, result: str, d: defer.Deferred['DeferredResponse'], port: str) -> None:
        if port: result += ':' + port
        self.packet(TRANSLATE_ADDRESS_STRING, result)
        d.callback(self)

    def http(self, uri: str, addresses: Collection[str]|None=None) -> defer.Deferred['DeferredResponse']|None:
        """Non-blocking version of Response.http() - returns a
        Deferred if the resolver is used."""
        assert uri[0] != '/' or addresses is None or len(addresses) == 0

        self.packet(TRANSLATE_HTTP, uri)

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            d: defer.Deferred['DeferredResponse'] = defer.Deferred()

            host, port = (urlparse(uri)[1].split(':', 1) + [None])[0:2]
            e = getHostByName(host)
            e.addCallback(self._resolve_success, d, port)
            e.addErrback(d.errback)

            return d

        return None

    proxy = http # deprecated

    def ajp(self, uri: str, addresses: Collection[str]|None=None) -> defer.Deferred['DeferredResponse']|None:
        """Non-blocking version of Response.http() - returns a
        Deferred if the resolver is used."""
        assert uri[0] != '/' or addresses is None or len(addresses) == 0
        assert addresses is None or hasattr(addresses, '__iter__')

        self.packet(TRANSLATE_AJP, uri)

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            d: defer.Deferred[DeferredResponse] = defer.Deferred()

            host, port = (uri.split(':', 1) + [None])[:2]
            assert isinstance(host, str)
            e = getHostByName(host)
            e.addCallback(self._resolve_success, d, port)
            e.addErrback(d.errback)

            return d

        return None
