#!/usr/bin/python
#
# Prototype for the beng-proxy translation server.
#
# Author: Max Kellermann <mk@cm4all.com>

from twisted.internet import reactor
from twisted.internet.protocol import Protocol, Factory
import struct

TRANSLATE_END = 1
TRANSLATE_BEGIN = 2
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

class PacketReader:
    def __init__(self):
        self._header = ''
        self.complete = False

    def consume(self, data):
        assert not self.complete
        assert isinstance(data, str)
        # read header
        if len(self._header) < 4:
            size = 4 - len(self._header)
            self._header += data[0:size]
            data = data[size:]
            if len(self._header) < 4:
                return data
            self._length, self.command = struct.unpack('HH', self._header)
            self.payload = ''
            if self._length == 0:
                self.complete = True
                return data
        # read payload
        if len(self.payload) < self._length:
            size = self._length - len(self.payload)
            self.payload += data[0:size]
            data = data[size:]
            if len(self.payload) == self._length:
                self.complete = True
        return data

class Request:
    def __init__(self):
        self.host = None
        self.uri = None
        self.session = None

    def packetReceived(self, packet):
        if packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload
        elif packet.command == TRANSLATE_URI:
            self.uri = packet.payload
        elif packet.command == TRANSLATE_SESSION:
            self.session = packet.payload
        else:
            print "Invalid command:", packet.command
        return False

class Translation(Protocol):
    def connectionMade(self):
        print "Connected from", self.transport.client
        self.transport.write('<hits>')
        self._request = None
        self._packet = None

    def _write_packet(self, command, payload = ''):
        assert isinstance(payload, str)
        self.transport.write(struct.pack('HH', len(payload), command))
        self.transport.write(payload)

    def _handle_request(self, request):
        self._write_packet(TRANSLATE_BEGIN)
        self._write_packet(TRANSLATE_PATH, '/var/www/beng.html')
        self._write_packet(TRANSLATE_PROCESS)
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
    reactor.listenUNIX('/tmp/beng-proxy-translate', factory)
    reactor.run()
