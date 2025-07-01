#
# A builder for a translation response.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

import six
import array
import struct
from typing import Collection, Iterable, Tuple
from urllib.parse import urlparse

from .protocol import *
from .serialize import packet_header

class Response:
    """Generator for a translation response.  The BEGIN and END
    packets are generated automatically.  When you are done with the
    response, call finish().  This method returns the full response
    (all serialized packets) as a string."""

    def __init__(self, protocol_version: int=0):
        assert protocol_version >= 0
        assert protocol_version <= 0xff

        self._data = b''

        payload = b''
        if protocol_version > 0:
            payload = struct.pack('B', protocol_version)
        self.packet(TRANSLATE_BEGIN, payload)

    def finish(self) -> bytes:
        """Finish the response, and return it as a string."""
        self._data += packet_header(TRANSLATE_END)
        return self._data

    def packet(self, command: int, payload: bytes|str = b'') -> 'Response':
        """Append a packet."""

        # this automatic conversion allows passing a `str` payload in
        # Python 3
        payload = six.ensure_binary(payload)

        self._data += packet_header(command, len(payload))
        self._data += payload
        return self

    def status(self, status: int) -> 'Response':
        """Append a STATUS packet."""
        assert status >= 200 and status < 600
        return self.packet(TRANSLATE_STATUS, struct.pack('H', status))

    def https_only(self, port: int=0) -> 'Response':
        """Append a HTTPS_ONLY packet."""
        return self.packet(TRANSLATE_HTTPS_ONLY, struct.pack('H', port))

    def redirect(self, url: str) -> 'Response':
        """Append a REDIRECT packet."""
        return self.packet(TRANSLATE_REDIRECT, url)

    def message(self, msg: str) -> 'Response':
        """Append a MESSAGE packet."""
        return self.packet(TRANSLATE_MESSAGE, msg.encode('us-ascii'))

    def view(self, name: str) -> 'Response':
        """Append a VIEW packet."""
        assert len(name) > 0
        return self.packet(TRANSLATE_VIEW, name)

    def process(self, container: bool=False) -> 'Response':
        """Append a PROCESS packet, and also a CONTAINER packet if the
        'container' argument is true."""
        self.packet(TRANSLATE_PROCESS)
        if container:
            self.packet(TRANSLATE_CONTAINER)
        return self

    def __http(self, packet: int, uri: str, addresses: Collection[str]|None=None) -> 'Response':
        assert uri[0] != '/' or addresses is None or len(addresses) == 0

        if uri[0] != '/' and addresses is None:
            # parse host:port from URL
            from socket import gethostbyname

            host, port = (urlparse(uri)[1].split(':', 1) + [None])[0:2]
            assert isinstance(host, str)
            address = gethostbyname(host)
            if port: address += ':' + port
            addresses = (address,)

        self.packet(packet, uri)
        if addresses is not None:
            for address in addresses:
                self.packet(TRANSLATE_ADDRESS_STRING, address)
        return self

    def http(self, uri: str, addresses: Collection[str]|None=None) -> 'Response':
        """Generate a HTTP packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""
        return self.__http(TRANSLATE_HTTP, uri, addresses)

    proxy = http # deprecated

    def ajp(self, uri: str, addresses: str|Iterable[str]) -> 'Response':
        """Generate an AJP packet.  If you do not specify an address
        list, this function looks up the URI's host name with the
        local resolver (which may throw socket.gaierror)."""

        if isinstance(addresses, str):
            from socket import gethostbyname
            host, port = (addresses.split(':', 1) + [None])[:2]
            assert isinstance(host, str)
            address = gethostbyname(host)
            if port: address += ':' + port
            addresses = (address,)

        self.packet(TRANSLATE_AJP, uri)
        for address in addresses:
            self.packet(TRANSLATE_ADDRESS_STRING, address)
        return self

    def nfs(self, server: str|bytes, export: str|bytes, path: str|bytes) -> 'Response':
        """Generate an NFS address."""

        self.packet(TRANSLATE_NFS_SERVER, server)
        self.packet(TRANSLATE_NFS_EXPORT, export)
        self.packet(TRANSLATE_PATH, path)
        return self

    def max_age(self, seconds: int) -> 'Response':
        return self.packet(TRANSLATE_MAX_AGE, struct.pack('I', seconds))

    def expires_relative(self, seconds: int) -> 'Response':
        return self.packet(TRANSLATE_EXPIRES_RELATIVE, struct.pack('I', seconds))

    def expires_relative_with_query(self, seconds: int) -> 'Response':
        return self.packet(TRANSLATE_EXPIRES_RELATIVE_WITH_QUERY, struct.pack('I', seconds))

    def vary(self, *args: int) -> 'Response':
        """Send a VARY packet.  All arguments are packet ids which are
        put into the VARY packet payload."""

        assert len(args) > 0
        payload = array.array('H', args)
        return self.packet(TRANSLATE_VARY, payload.tobytes())

    def invalidate(self, *args: int) -> 'Response':
        """Send a INVALIDATE packet.  All arguments are packet ids
        which are put into the INVALIDATE packet payload."""

        assert len(args) > 0
        payload = array.array('H', args)
        return self.packet(TRANSLATE_INVALIDATE, payload.tobytes())

    def want(self, *args: int) -> 'Response':
        """Send a WANT packet.  All arguments are packet ids which are
        put into the WANT packet payload."""

        assert len(args) > 0
        payload = array.array('H', args)
        return self.packet(TRANSLATE_WANT, payload.tobytes())

    def pipe(self, path: str|bytes, *args: str) -> 'Response':
        """Send a PIPE packet.  You may pass additional arguments
        which are sent as APPEND packets."""

        assert len(path) > 0
        self.packet(TRANSLATE_PIPE, path)
        for arg in args:
            self.packet(TRANSLATE_APPEND, arg)
        return self

    def path(self, path: str|bytes) -> 'Response':
        assert len(path) > 0
        assert path[0] == '/' or path[0] == ord('/')
        return self.packet(TRANSLATE_PATH, path)

    def gzipped(self, path: str|bytes) -> 'Response':
        assert len(path) > 0
        assert path[0] == '/' or path[0] == ord('/')
        return self.packet(TRANSLATE_GZIPPED, path)

    def pair(self, name: str, value: str|bytes) -> 'Response':
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_PAIR,
                           six.ensure_binary(name) + b'=' + six.ensure_binary(value))

    def expand_pair(self, name: str, value: str|bytes) -> 'Response':
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_EXPAND_PAIR,
                           six.ensure_binary(name) + b'=' + six.ensure_binary(value))

    def setenv(self, name: str, value: str|bytes) -> 'Response':
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_SETENV,
                           six.ensure_binary(name) + b'=' + six.ensure_binary(value))

    def expand_setenv(self, name: str, value: str|bytes) -> 'Response':
        assert len(name) > 0
        assert name.find('=') < 0
        return self.packet(TRANSLATE_EXPAND_SETENV,
                           six.ensure_binary(name) + b'=' + six.ensure_binary(value))

    def content_type(self, content_type: str) -> 'Response':
        assert content_type.find('/') > 0
        return self.packet(TRANSLATE_CONTENT_TYPE, content_type)

    def header_forward(self, command: int, *args: Tuple[int, int]) -> 'Response':
        payload = b''
        for x in args:
            payload += struct.pack('hBx', *x)
        return self.packet(command, payload)

    def request_header_forward(self, *args: Tuple[int, int]) -> 'Response':
        return self.header_forward(TRANSLATE_REQUEST_HEADER_FORWARD,
                                   *args)

    def response_header_forward(self, *args: Tuple[int, int]) -> 'Response':
        return self.header_forward(TRANSLATE_RESPONSE_HEADER_FORWARD,
                                   *args)

    def response_header(self, name: str|bytes, value: str|bytes) -> 'Response':
        return self.packet(TRANSLATE_HEADER,
                           six.ensure_binary(name) + b':' + six.ensure_binary(value))

    def request_header(self, name: str|bytes, value: str|bytes) -> 'Response':
        return self.packet(TRANSLATE_REQUEST_HEADER,
                           six.ensure_binary(name) + b':' + six.ensure_binary(value))

    def expand_request_header(self, name: str|bytes, value: str|bytes) -> 'Response':
        return self.packet(TRANSLATE_EXPAND_REQUEST_HEADER,
                           six.ensure_binary(name) + b':' + six.ensure_binary(value))

    def header(self, name: str|bytes, value: str|bytes) -> 'Response':
        """Deprecated.  Use response_header() instead."""
        return self.response_header(name, value)

    def validate_mtime(self, mtime: int, path: str|bytes) -> 'Response':
        return self.packet(TRANSLATE_VALIDATE_MTIME,
                           struct.pack('L', int(mtime)) + six.ensure_binary(path))

    def bind_mount(self, source: str|bytes, target: str|bytes,
                   expand: bool=False, writable: bool=False) -> 'Response':
        source = six.ensure_binary(source)
        assert source[0] == ord('/')

        target = six.ensure_binary(target)
        assert target[0] == ord('/')

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

        return self.packet(command, source + b'\0' + target)

    def umask(self, umask: int) -> 'Response':
        """Append a UMASK packet."""
        return self.packet(TRANSLATE_UMASK, struct.pack('H', umask))

    def uid_gid(self, uid: int, gid: int, *supplementary_groups: int) -> 'Response':
        return self.packet(TRANSLATE_UID_GID,
                           struct.pack(str(2 + len(supplementary_groups)) + 'I',
                                       uid, gid, *supplementary_groups))

    def external_session_manager(self, uri: str, addresses: Collection[str]|None=None) -> 'Response':
        return self.__http(TRANSLATE_EXTERNAL_SESSION_MANAGER, uri, addresses)

    def external_session_keepalive(self, interval: int) -> 'Response':
        return self.packet(TRANSLATE_EXTERNAL_SESSION_KEEPALIVE, struct.pack('H', interval))

    def allow_remote_network(self, address: str, prefix_length: int) -> 'Response':
        import socket
        octets = map(int, address.split('.'))
        payload = struct.pack('B', prefix_length) + struct.pack('HHBBBBxxxxxxxx', socket.AF_INET, 0, *octets)
        print(repr(payload))
        return self.packet(TRANSLATE_ALLOW_REMOTE_NETWORK, payload)
