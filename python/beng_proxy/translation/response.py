#
# A builder for a translation response.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import array, struct

try:
    from urllib.parse import urlparse
except ImportError:
    from urlparse import urlparse

from .protocol import *
from .serialize import packet_header

class Response:
    """Generator for a translation response.  The BEGIN and END
    packets are generated automatically.  When you are done with the
    response, call finish().  This method returns the full response
    (all serialized packets) as a string."""

    def __init__(self, protocol_version=0):
        assert isinstance(protocol_version, int)
        assert protocol_version >= 0
        assert protocol_version <= 0xff

        self._data = ''

        payload = ''
        if protocol_version > 0:
            payload = struct.pack('B', protocol_version)
        self.packet(TRANSLATE_BEGIN, payload)

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

    def https_only(self, port=0):
        """Append a HTTPS_ONLY packet."""
        return self.packet(TRANSLATE_HTTPS_ONLY, struct.pack('H', port))

    def redirect(self, url):
        """Append a REDIRECT packet."""
        return self.packet(TRANSLATE_REDIRECT, url)

    def message(self, msg):
        """Append a MESSAGE packet."""
        return self.packet(TRANSLATE_MESSAGE, msg.encode('us-ascii'))

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

    def __http(self, packet, uri, addresses=None):
        assert uri[0] != '/' or len(addresses) == 0
        assert addresses is None or hasattr(addresses, '__iter__')

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            from socket import gethostbyname

            host, port = (urlparse(uri)[1].split(':', 1) + [None])[0:2]
            address = gethostbyname(host)
            if port: address += ':' + port
            addresses = (address,)

        self.packet(packet, uri)
        for address in addresses:
            self.packet(TRANSLATE_ADDRESS_STRING, address)
        return self

    def http(self, *args, **kwargs):
        """Generate a HTTP packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""
        return self.__http(TRANSLATE_HTTP, *args, **kwargs)

    proxy = http # deprecated

    def ajp(self, uri, addresses):
        """Generate an AJP packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""

        assert isinstance(addresses, str) or hasattr(addresses, '__iter__')

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

    def nfs(self, server, export, path):
        """Generate an NFS address."""

        assert isinstance(server, str)
        assert isinstance(export, str)
        assert isinstance(path, str)

        self.packet(TRANSLATE_NFS_SERVER, server)
        self.packet(TRANSLATE_NFS_EXPORT, export)
        self.packet(TRANSLATE_PATH, path)
        return self

    def max_age(self, seconds):
        assert isinstance(seconds, int)
        return self.packet(TRANSLATE_MAX_AGE, struct.pack('I', seconds))

    def expires_relative(self, seconds):
        assert isinstance(seconds, int)
        return self.packet(TRANSLATE_EXPIRES_RELATIVE, struct.pack('I', seconds))

    def vary(self, *args):
        """Send a VARY packet.  All arguments are packet ids which are
        put into the VARY packet payload."""

        assert len(args) > 0
        payload = array.array('H', args).tostring()
        return self.packet(TRANSLATE_VARY, payload)

    def invalidate(self, *args):
        """Send a INVALIDATE packet.  All arguments are packet ids
        which are put into the INVALIDATE packet payload."""

        assert len(args) > 0
        payload = array.array('H', args).tostring()
        return self.packet(TRANSLATE_INVALIDATE, payload)

    def want(self, *args):
        """Send a WANT packet.  All arguments are packet ids which are
        put into the WANT packet payload."""

        assert len(args) > 0
        payload = array.array('H', args).tostring()
        return self.packet(TRANSLATE_WANT, payload)

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

    def expand_pair(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_EXPAND_PAIR, name + '=' + value)

    def setenv(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_SETENV, name + '=' + value)

    def expand_setenv(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_EXPAND_SETENV, name + '=' + value)

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

    def response_header(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        return self.packet(TRANSLATE_HEADER, name + ':' + value)

    def request_header(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        return self.packet(TRANSLATE_REQUEST_HEADER, name + ':' + value)

    def expand_request_header(self, name, value):
        assert isinstance(name, str)
        assert isinstance(value, str)
        return self.packet(TRANSLATE_EXPAND_REQUEST_HEADER, name + ':' + value)

    def header(self, name, value):
        """Deprecated.  Use response_header() instead."""
        return self.response_header(name, value)

    def validate_mtime(self, mtime, path):
        return self.packet(TRANSLATE_VALIDATE_MTIME,
                           struct.pack('L', mtime) + path)

    def bind_mount(self, source, target, expand=False, writable=False):
        assert isinstance(source, str)
        assert isinstance(target, str)
        assert source[0] == '/'
        assert target[0] == '/'

        if writable:
            if expand:
                command = TRANSLATE_EXPAND_BIND_MOUNT_RW
            else:
                command = TRANSLATE_BIND_MOUNT_RW
        else:
            if expand:
                command = TRANSLATE_EXPAND_BIND_MOUNT
            else:
                command = TRANSLATE_BIND_MOUNT

        return self.packet(command, source + '\0' + target)

    def umask(self, umask):
        """Append a UMASK packet."""
        return self.packet(TRANSLATE_UMASK, struct.pack('H', umask))

    def uid_gid(self, uid, gid, *supplementary_groups):
        assert isinstance(uid, int)
        assert isinstance(gid, int)
        return self.packet(TRANSLATE_UID_GID,
                           struct.pack(str(2 + len(supplementary_groups)) + 'I',
                                       uid, gid, *supplementary_groups))

    def external_session_manager(self, *args, **kwargs):
        return self.__http(TRANSLATE_EXTERNAL_SESSION_MANAGER, *args, **kwargs)

    def external_session_keepalive(self, interval):
        return self.packet(TRANSLATE_EXTERNAL_SESSION_KEEPALIVE, struct.pack('H', interval))
