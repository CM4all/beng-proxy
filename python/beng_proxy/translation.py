# Author: Max Kellermann <mk@cm4all.com>

import struct
import urllib

TRANSLATE_BEGIN = 1
TRANSLATE_END = 2
TRANSLATE_HOST = 3
TRANSLATE_URI = 4
TRANSLATE_STATUS = 5
TRANSLATE_PATH = 6
TRANSLATE_CONTENT_TYPE = 7
TRANSLATE_PROXY = 8
TRANSLATE_REDIRECT = 9
TRANSLATE_FILTER = 10
TRANSLATE_PROCESS = 11
TRANSLATE_SESSION = 12
TRANSLATE_PARAM = 13
TRANSLATE_USER = 14
TRANSLATE_LANGUAGE = 15
TRANSLATE_REMOTE_HOST = 16
TRANSLATE_PATH_INFO = 17
TRANSLATE_SITE = 18
TRANSLATE_CGI = 19
TRANSLATE_DOCUMENT_ROOT = 20
TRANSLATE_WIDGET_TYPE = 21
TRANSLATE_CONTAINER = 22
TRANSLATE_ADDRESS = 23
TRANSLATE_ADDRESS_STRING = 24
TRANSLATE_JAILCGI = 26
TRANSLATE_INTERPRETER = 27
TRANSLATE_ACTION = 28
TRANSLATE_SCRIPT_NAME = 29
TRANSLATE_AJP = 30
TRANSLATE_DOMAIN = 31
TRANSLATE_STATEFUL = 32
TRANSLATE_FASTCGI = 33
TRANSLATE_VIEW = 34
TRANSLATE_USER_AGENT = 35
TRANSLATE_MAX_AGE = 36
TRANSLATE_VARY = 37
TRANSLATE_QUERY_STRING = 38
TRANSLATE_PIPE = 39
TRANSLATE_BASE = 40
TRANSLATE_DELEGATE = 41
TRANSLATE_INVALIDATE = 42
TRANSLATE_LOCAL_ADDRESS = 43
TRANSLATE_LOCAL_ADDRESS_STRING = 44
TRANSLATE_APPEND = 45
TRANSLATE_DISCARD_SESSION = 46

def _parse_port(address):
    if address[0] == '[':
        i = address.find(']', 1)
        if i < 0 or len(address) <= i + 2 or address[i + 1] != ':':
            return None
        port = address[i + 2:]
    else:
        i = address.find(':')
        if i < 0 or address.find(':', i + 1) > i:
            # more than one colon: IPv6 address without port
            return None
        port = address[i + 1:]
    try:
        return int(port)
    except ParserError:
        return None

class PacketReader:
    def __init__(self):
        self._header = ''
        self.complete = False

    def consume(self, data):
        assert not self.complete
        assert isinstance(data, str)
        # read header
        if len(self._header) < 4:
            # append to header
            size = 4 - len(self._header)
            self._header += data[0:size]
            data = data[size:]
            if len(self._header) < 4:
                return data

            # header is finished, decode it
            self._length, self.command = struct.unpack('HH', self._header)
            self.payload = ''
            if self._length == 0:
                # no payload, we're done
                self.complete = True
                return data

        # read payload
        if len(self.payload) < self._length:
            # append to payload
            size = self._length - len(self.payload)
            self.payload += data[0:size]
            data = data[size:]

            # done yet?
            if len(self.payload) == self._length:
                self.complete = True

        # return data chunk without the consumed part
        return data

class Request:
    def __init__(self):
        self.host = None
        self.raw_uri = None
        self.query_string = None
        self.widget_type = None
        self.session = None
        self.param = None
        self.local_address = None
        self.local_port = None
        self.remote_host = None
        self.user_agent = None
        self.accept_language = None

    def __getattr__(self, name):
        if name == 'uri':
            # compatibility with pre-0.7: return the unquoted URI
            if self.raw_uri is None:
                return None
            return urllib.unquote(self.raw_uri)
        else:
            raise AttributeError(name)

    def packetReceived(self, packet):
        if packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload
        elif packet.command == TRANSLATE_URI:
            self.raw_uri = packet.payload
        elif packet.command == TRANSLATE_QUERY_STRING:
            self.query_string = packet.payload
        elif packet.command == TRANSLATE_WIDGET_TYPE:
            self.widget_type = packet.payload
        elif packet.command == TRANSLATE_SESSION:
            self.session = packet.payload
        elif packet.command == TRANSLATE_PARAM:
            self.param = packet.payload
        elif packet.command == TRANSLATE_LOCAL_ADDRESS_STRING:
            self.local_address = packet.payload
            self.local_port = _parse_port(self.local_address)
        elif packet.command == TRANSLATE_REMOTE_HOST:
            self.remote_host = packet.payload
        elif packet.command == TRANSLATE_USER_AGENT:
            self.user_agent = packet.payload
        elif packet.command == TRANSLATE_LANGUAGE:
            self.accept_language = packet.payload
        elif packet.command != TRANSLATE_LOCAL_ADDRESS:
            print "Invalid command:", packet.command
        return False

def packet_header(command, length=0):
    assert length <= 0xffff
    return struct.pack('HH', length, command)

def write_packet(f, command, payload = ''):
    assert isinstance(payload, str)
    f.write(packet_header(command, len(payload)))
    f.write(payload)

class Response:
    def __init__(self):
        self._data = packet_header(TRANSLATE_BEGIN)

    def finish(self):
        self._data += packet_header(TRANSLATE_END)
        return self._data

    def packet(self, command, payload = ''):
        assert isinstance(payload, str)
        self._data += packet_header(command, len(payload))
        self._data += payload

    def status(self, status):
        assert status >= 200 and status < 600
        self.packet(TRANSLATE_STATUS, struct.pack('H', status))

    def view(self, name):
        assert isinstance(name, str)
        assert len(name) > 0
        self.packet(TRANSLATE_VIEW, name)

    def process(self, container=False):
        self.packet(TRANSLATE_PROCESS)
        if container:
            self.packet(TRANSLATE_CONTAINER)

    def proxy(self, uri, addresses=None):
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

    def ajp(self, uri, addresses):
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

    def vary(self, *args):
        assert len(args) > 0
        payload = ''.join(map(lambda x: struct.pack('H', x), args))
        self.packet(TRANSLATE_VARY, payload)

    def invalidate(self, *args):
        assert len(args) > 0
        payload = ''.join(map(lambda x: struct.pack('H', x), args))
        self.packet(TRANSLATE_INVALIDATE, payload)

    def pipe(self, path, *args):
        assert isinstance(path, str)
        assert len(path) > 0
        self.packet(TRANSLATE_PIPE, path)
        for arg in args:
            self.packet(TRANSLATE_APPEND, arg)

    def path(self, path):
        assert isinstance(path, str)
        assert len(path) > 0
        assert path[0] == '/'
        self.packet(TRANSLATE_PATH, path)

    def content_type(self, content_type):
        assert isinstance(content_type, str)
        assert content_type.find('/') > 0
        self.packet(TRANSLATE_CONTENT_TYPE, content_type)

    def delegate(self, helper):
        assert isinstance(helper, str)
        self.packet(TRANSLATE_DELEGATE, helper)

    def delegated_path(self, helper, path):
        self.path(path)
        self.delegate(helper)
