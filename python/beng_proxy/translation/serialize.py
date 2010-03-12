#
# An implementation of the beng-proxy translation server protocol.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import struct

class PacketReader:
    """A class which can read a packet incrementally.  Whenever you
    receive a chunk of data from the socket, call the method
    consume().  As soon as the attribute 'finished' becomes true, the
    attributes 'command' and 'payload' are available."""

    def __init__(self):
        self._header = ''
        self.complete = False

    def consume(self, data):
        """Consumes a chunk of data.  Returns the unused tail of the
        buffer.  This must not be called when the 'complete' attribute
        is already true."""

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

def packet_header(command, length=0):
    """Generate the header of a translation packet."""

    assert length <= 0xffff
    return struct.pack('HH', length, command)

def write_packet(f, command, payload = ''):
    assert isinstance(payload, str)
    f.write(packet_header(command, len(payload)))
    f.write(payload)
