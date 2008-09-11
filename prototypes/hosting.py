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
from beng_proxy.translation import *

cgi_re = re.compile('\.(?:sh|rb|py|pl|cgi|php\d?)$')

class Translation(Protocol):
    def connectionMade(self):
        print "Connected from", self.transport.client
        self._request = None
        self._packet = None

    def _write_packet(self, command, payload = ''):
        assert isinstance(payload, str)
        write_packet(self.transport, command, payload)

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
