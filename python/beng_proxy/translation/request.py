#
# Incremental request parser.
#
# Author: Max Kellermann <mk@cm4all.com>
#

import struct
import urllib
from beng_proxy.translation.protocol import *
import beng_proxy.translation.uri

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

class Request:
    """An OO wrapper for a translation request.  This object is empty
    when created, and is completed incrementally by calling
    packetReceived() until it returns true.

    Never ever access the 'args' property."""

    def __init__(self):
        self.host = None
        self.raw_uri = None
        self.args = None
        self.query_string = None
        self.widget_type = None
        self.session = None
        self.param = None
        self.local_address = None
        self.local_port = None
        self.remote_host = None
        self.user_agent = None
        self.accept_language = None
        self.authorization = None
        self.status = None
        self.error_document = False

    def __getattr__(self, name):
        if name == 'uri':
            # compatibility with pre-0.7: return the unquoted URI
            if self.raw_uri is None:
                return None
            return urllib.unquote(self.raw_uri)
        else:
            raise AttributeError(name)

    def packetReceived(self, packet):
        """Feed a packet into this object.  Returns true when the
        request is finished."""

        if packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload
        elif packet.command == TRANSLATE_URI:
            self.raw_uri = packet.payload
        elif packet.command == TRANSLATE_ARGS:
            self.args = packet.payload
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
        elif packet.command == TRANSLATE_AUTHORIZATION:
            self.authorization = packet.payload
        elif packet.command == TRANSLATE_STATUS:
            if len(packet.payload) == 2:
                self.status = struct.unpack('H', packet.payload)[0]
        elif packet.command == TRANSLATE_ERROR_DOCUMENT:
            self.error_document = True
        elif packet.command != TRANSLATE_LOCAL_ADDRESS:
            print "Invalid command:", packet.command
        return False

    def absolute_uri(self, scheme=None, host=None, uri=None, query_string=None,
                     param=None):
        """Returns the absolute URI of this request.  You may override
        some of the attributes."""
        return beng_proxy.translation.uri.absolute_uri(self, scheme=scheme,
                                                       host=host, uri=uri,
                                                       query_string=query_string,
                                                       param=param)
