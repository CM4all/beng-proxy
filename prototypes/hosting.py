#!/usr/bin/python
#
# Prototype for the beng-proxy translation server, to demonstrate
# beng-proxy based web hosting.
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
TRANSLATE_JAILCGI = 26
TRANSLATE_INTERPRETER = 27
TRANSLATE_ACTION = 28
TRANSLATE_SCRIPT_NAME = 29
TRANSLATE_AJP = 30
TRANSLATE_DOMAIN = 31
TRANSLATE_STATEFUL = 31

cgi_re = re.compile('\.(?:sh|rb|py|pl|cgi|php\d?)$')

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

    def _host_account_id(self, host):
        if not host: return None
        if host[-26:] != '.hosting.intern.cm-ag:8080': return None
        host = host[:-26]
        if len(host) != 12: return None
        return host.upper()

    def _handle_request(self, request):
        if request.widget_type is not None:
            self._write_packet(TRANSLATE_BEGIN)
            self._write_packet(TRANSLATE_STATUS, struct.pack('H', 404))
            self._write_packet(TRANSLATE_END)
            return

        account_id = self._host_account_id(request.host)
        if not account_id:
            self._write_packet(TRANSLATE_BEGIN)
            self._write_packet(TRANSLATE_STATUS, struct.pack('H', 404))
            self._write_packet(TRANSLATE_END)
            return

        document_root = '/var/www/vol1/%s/%s/%s/%s/pr_0001' % \
                        (account_id[0:6], account_id[6:8],
                         account_id[8:10], account_id[10:12])
        path = document_root + request.uri
        if path[-1:] == '/': path += 'index.html'

        cgi = cgi_re.search(path, 1)

        self._write_packet(TRANSLATE_BEGIN)
        self._write_packet(TRANSLATE_DOCUMENT_ROOT, document_root)
        if cgi:
            self._write_packet(TRANSLATE_CGI, path)
            self._write_packet(TRANSLATE_JAILCGI)
            if path[-3:] == '.sh':
                self._write_packet(TRANSLATE_INTERPRETER, '/bin/bash')
            elif path[-3:] == '.pl':
                self._write_packet(TRANSLATE_INTERPRETER, '/usr/bin/perl')
            elif path[-3:] == '.py':
                self._write_packet(TRANSLATE_INTERPRETER, '/usr/bin/python')
            elif path[-3:] == '.rb':
                self._write_packet(TRANSLATE_INTERPRETER, '/usr/bin/ruby')
            elif path[-4:] == '.php':
                self._write_packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-phpchooser')
            elif path[-5:] == '.php4':
                self._write_packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-php4wrapper/')
            elif path[-5:] == '.php5':
                self._write_packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-php5wrapper/')
        else:
            self._write_packet(TRANSLATE_PATH, path)
        if not cgi and path[-5:] == '.html':
            self._write_packet(TRANSLATE_CONTENT_TYPE, 'text/html; charset=utf-8')
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
