#!/usr/bin/python
#
# Prototype for the beng-proxy translation server.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
from twisted.internet import reactor
from twisted.internet.protocol import Protocol, Factory
import struct
from urlparse import urlparse
from socket import gethostbyname

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
TRANSLATE_GOOGLE_GADGET = 25

cgi_re = re.compile('\.(?:sh|rb|py|pl|cgi)$')

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
        self.uri = None
        self.widget_type = None
        self.session = None
        self.param = None
        self.remote_host = None

    def packetReceived(self, packet):
        if packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload
        elif packet.command == TRANSLATE_URI:
            self.uri = packet.payload
        elif packet.command == TRANSLATE_WIDGET_TYPE:
            self.widget_type = packet.payload
        elif packet.command == TRANSLATE_SESSION:
            self.session = packet.payload
        elif packet.command == TRANSLATE_PARAM:
            self.param = packet.payload
        elif packet.command == TRANSLATE_REMOTE_HOST:
            self.remote_host = packet.payload
        else:
            print "Invalid command:", packet.command
        return False

class Translation(Protocol):
    def connectionMade(self):
        print "Connected from", self.transport.client
        self._request = None
        self._packet = None

    def _write_packet(self, command, payload = ''):
        assert isinstance(payload, str)
        self.transport.write(struct.pack('HH', len(payload), command))
        self.transport.write(payload)

    def _handle_widget_lookup(self, request):
        # checks on the name should be here.
        path = "/etc/cm4all/beng/widgets/%s" % request.widget_type
        try:
            f = open(path)
        except IOError:
            self._write_packet(TRANSLATE_BEGIN)
            self._write_packet(TRANSLATE_STATUS, struct.pack('H', 404))
            self._write_packet(TRANSLATE_END)
            return
        self._write_packet(TRANSLATE_BEGIN)
        for line in f.readlines():
            line = line.strip()
            if line == '' or line[0] == '#':
                continue
            m = re.match(r'^server\s+"(\S+)"$', line)
            if m:
                uri = m.group(1)
                self._write_packet(TRANSLATE_PROXY, uri)
                host, port = urlparse(uri)[1].split(':', 1) + [None]
                address = gethostbyname(host)
                if port: address += ':' + port
                self._write_packet(TRANSLATE_ADDRESS_STRING, address)
                continue
            if line == 'process':
                self._write_packet(TRANSLATE_PROCESS)
            elif line == 'container':
                self._write_packet(TRANSLATE_CONTAINER)
            elif line == 'google_gadget':
                self._write_packet(TRANSLATE_GOOGLE_GADGET)
            else:
                print "Syntax error in %s: %s" % (path, line)
                self._write_packet(TRANSLATE_STATUS, struct.pack('H', 500))
                break
        self._write_packet(TRANSLATE_END)

    def _handle_request(self, request):
        if request.session is not None: print "- session =", request.session
        if request.param is not None: print "- param =", request.param

        if request.param is not None:
            # log in or log out; "real" authentification is missing
            # here.  We're logging out if request.param is an empty
            # string.
            user = session = request.param
        elif request.session is not None:
            # user is already authenticated
            user = request.session
            session = None
        else:
            # 
            user = session = None

        if request.widget_type is not None:
            return self._handle_widget_lookup(request)

        if request.uri[:19] == '/cm4all-beng-proxy/':
            from sys import argv
            if len(argv) >= 3:
                path = argv[2]
            else:
                from os.path import abspath, dirname, join
                path = join(dirname(dirname(abspath(argv[0]))), 'js/')
            path += request.uri[19:]
        else:
            path = '/var/www' + request.uri

        cgi = cgi_re.search(path, 1)

        self._write_packet(TRANSLATE_BEGIN)
        self._write_packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
        self._write_packet(TRANSLATE_PATH, path)
        if cgi:
            self._write_packet(TRANSLATE_CGI)
        if user is not None:
            self._write_packet(TRANSLATE_USER, user)
        if session is not None:
            self._write_packet(TRANSLATE_SESSION, session)
        #self._write_packet(TRANSLATE_FILTER, 'http://cfatest01.intern.cm-ag/filter.py')
        if not cgi and path[-5:] == '.html':
            self._write_packet(TRANSLATE_CONTENT_TYPE, 'text/html; charset=utf-8')
            self._write_packet(TRANSLATE_PROCESS)
            self._write_packet(TRANSLATE_CONTAINER)
        self._write_packet(TRANSLATE_END)

    def _handle_packet(self, packet):
        if packet.command == TRANSLATE_BEGIN:
            self._request = Request()
        elif self._request is not None:
            if self._request.packetReceived(packet):
                self._handle_request(self._request)
                self._request = None
        else:
            print "Invalid command without request:", packet.command

    def dataReceived(self, data):
        while len(data) > 0:
            if self._packet is None:
                self._packet = PacketReader()
            data = self._packet.consume(data)
            assert len(data) == 0 or self._packet.complete
            if self._packet.complete:
                self._handle_packet(self._packet)
                self._packet = None

    def connectionLost(self, reason):
        print "Disconnected from", self.transport.client

factory = Factory()
factory.protocol = Translation

if __name__ == '__main__':
    from sys import argv
    if len(argv) >= 2:
        path = argv[1]
    else:
        path = '/tmp/beng-proxy-translate'
    reactor.listenUNIX(path, factory)
    reactor.run()
