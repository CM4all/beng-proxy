#!/usr/bin/python
#
# Prototype for the beng-proxy translation server, to demonstrate
# beng-proxy based web hosting.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
from twisted.python import log
from twisted.internet import reactor
from twisted.internet.protocol import Protocol, Factory
from beng_proxy.translation import *

cgi_re = re.compile('\.(?:sh|rb|py|pl|cgi|php\d?)$')

class Translation(Protocol):
    def connectionMade(self):
        log.msg("Connected from %s" % str(self.transport.client))
        self._request = None
        self._packet = None

    def _host_account_id(self, host):
        if not host: return None
        if host[-26:] != '.hosting.intern.cm-ag:8080': return None
        host = host[:-26]
        if len(host) != 12: return None
        return host.upper()

    def _handle_request(self, request):
        if request.widget_type is not None:
            response = Response()
            response.status(404)
            self.transport.write(response.finish())
            return

        account_id = self._host_account_id(request.host)
        if not account_id:
            response = Response()
            response.status(404)
            self.transport.write(response.finish())
            return

        document_root = '/var/www/vol1/%s/%s/%s/%s/pr_0001' % \
                        (account_id[0:6], account_id[6:8],
                         account_id[8:10], account_id[10:12])
        path = document_root + request.uri
        if path[-1:] == '/': path += 'index.html'

        cgi = cgi_re.search(path, 1)

        response = Response()
        response.packet(TRANSLATE_DOCUMENT_ROOT, document_root)
        if cgi:
            response.packet(TRANSLATE_CGI, path)
            response.packet(TRANSLATE_JAILCGI)
            if path[-3:] == '.sh':
                response.packet(TRANSLATE_INTERPRETER, '/bin/bash')
            elif path[-3:] == '.pl':
                response.packet(TRANSLATE_INTERPRETER, '/usr/bin/perl')
            elif path[-3:] == '.py':
                response.packet(TRANSLATE_INTERPRETER, '/usr/bin/python')
            elif path[-3:] == '.rb':
                response.packet(TRANSLATE_INTERPRETER, '/usr/bin/ruby')
            elif path[-4:] == '.php':
                response.packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-phpchooser')
            elif path[-5:] == '.php4':
                response.packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-php4wrapper/')
            elif path[-5:] == '.php5':
                response.packet(TRANSLATE_ACTION, '/usr/bin/cm4all-jailcgi-php5wrapper/')
        else:
            response.packet(TRANSLATE_PATH, path)
        if not cgi and path[-5:] == '.html':
            response.packet(TRANSLATE_CONTENT_TYPE, 'text/html; charset=utf-8')
        self.transport.write(response.finish())

    def _handle_packet(self, packet):
        if packet.command == TRANSLATE_BEGIN:
            self._request = Request()
            self._request.packetReceived(packet)
        elif self._request is not None:
            if self._request.packetReceived(packet):
                self._handle_request(self._request)
                self._request = None
        else:
            log.msg("Invalid command without request: %u" % packet.command)

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
        log.msg("Disconnected from %s" % str(self.transport.client))

factory = Factory()
factory.protocol = Translation

if __name__ == '__main__':
    from sys import argv
    if len(argv) >= 2:
        path = argv[1]
    else:
        path = '\0translation'

    observer = log.PythonLoggingObserver()
    observer.start()

    import sys, logging
    logger = logging.getLogger(None)
    logger.setLevel(logging.DEBUG)
    handler = logging.StreamHandler(sys.stderr)
    handler.setLevel(logging.DEBUG)
    formatter = logging.Formatter("%(asctime)s - %(message)s")
    handler.setFormatter(formatter)
    logger.addHandler(handler)

    reactor.listenUNIX(path, factory)
    reactor.run()
