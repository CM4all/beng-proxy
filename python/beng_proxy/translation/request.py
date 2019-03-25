#
# Incremental request parser.
#
# Author: Max Kellermann <mk@cm4all.com>
#

from __future__ import print_function
import array, struct

try:
    from urllib.parse import unquote
except ImportError:
    from urllib import unquote

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
        self.protocol_version = 0
        self.host = None
        self.alt_host = None
        self.raw_uri = None
        self.args = None
        self.query_string = None
        self.widget_type = None
        self.session = None
        self.check = None
        self.auth = None
        self.want_full_uri = None
        self.param = None
        self.listener_tag = None
        self.local_address = None
        self.local_port = None
        self.remote_host = None
        self.user_agent = None
        self.ua_class = None
        self.accept_language = None
        self.authorization = None
        self.status = None
        self.want = None
        self.file_not_found = None
        self.directory_index = None
        self.internal_redirect = None
        self.enotdir = None
        self.content_type_lookup = None
        self.suffix = None
        self.error_document = False
        self.error_document_payload = None
        self.probe_path_suffixes = None
        self.probe_suffix = None
        self.read_file = None
        self.pool = None
        self.user = None
        self.login = False
        self.password = None
        self.service = None
        self.cron = False

    def __getattr__(self, name):
        if name == 'uri':
            # compatibility with pre-0.7: return the unquoted URI
            if self.raw_uri is None:
                return None
            return unquote(self.raw_uri)
        else:
            raise AttributeError(name)

    def packetReceived(self, packet):
        """Feed a packet into this object.  Returns true when the
        request is finished."""

        if packet.command == TRANSLATE_BEGIN:
            if len(packet.payload) > 0:
                self.protocol_version = ord(packet.payload[0])
        elif packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload
        elif packet.command == TRANSLATE_ALT_HOST:
            self.alt_host = packet.payload
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
        elif packet.command == TRANSLATE_CHECK:
            self.check = packet.payload
        elif packet.command == TRANSLATE_AUTH:
            self.auth = packet.payload
        elif packet.command == TRANSLATE_WANT_FULL_URI:
            self.want_full_uri = packet.payload
        elif packet.command == TRANSLATE_PARAM:
            self.param = packet.payload
        elif packet.command == TRANSLATE_LISTENER_TAG:
            self.listener_tag = packet.payload
        elif packet.command == TRANSLATE_LOCAL_ADDRESS_STRING:
            self.local_address = packet.payload
            self.local_port = _parse_port(self.local_address)
        elif packet.command == TRANSLATE_REMOTE_HOST:
            self.remote_host = packet.payload
        elif packet.command == TRANSLATE_USER_AGENT:
            self.user_agent = packet.payload
        elif packet.command == TRANSLATE_UA_CLASS:
            self.ua_class = packet.payload
        elif packet.command == TRANSLATE_LANGUAGE:
            self.accept_language = packet.payload
        elif packet.command == TRANSLATE_AUTHORIZATION:
            self.authorization = packet.payload
        elif packet.command == TRANSLATE_STATUS:
            if len(packet.payload) == 2:
                self.status = struct.unpack('H', packet.payload)[0]
        elif packet.command == TRANSLATE_WANT:
            self.want = array.array('H')
            self.want.fromstring(packet.payload)
        elif packet.command == TRANSLATE_FILE_NOT_FOUND:
            self.file_not_found = packet.payload
        elif packet.command == TRANSLATE_DIRECTORY_INDEX:
            self.directory_index = packet.payload
        elif packet.command == TRANSLATE_INTERNAL_REDIRECT:
            self.internal_redirect = packet.payload
        elif packet.command == TRANSLATE_ENOTDIR:
            self.enotdir = packet.payload
        elif packet.command == TRANSLATE_CONTENT_TYPE_LOOKUP:
            self.content_type_lookup = packet.payload
        elif packet.command == TRANSLATE_SUFFIX:
            self.suffix = packet.payload
        elif packet.command == TRANSLATE_ERROR_DOCUMENT:
            self.error_document = True
            self.error_document_payload = packet.payload
        elif packet.command == TRANSLATE_PROBE_PATH_SUFFIXES:
            self.probe_path_suffixes = packet.payload
        elif packet.command == TRANSLATE_PROBE_SUFFIX:
            self.probe_suffix = packet.payload
        elif packet.command == TRANSLATE_READ_FILE:
            self.read_file = packet.payload
        elif packet.command == TRANSLATE_POOL:
            self.pool = packet.payload
        elif packet.command == TRANSLATE_USER:
            self.user = packet.payload
        elif packet.command == TRANSLATE_LOGIN:
            self.login = True
        elif packet.command == TRANSLATE_PASSWORD:
            self.password = packet.payload
        elif packet.command == TRANSLATE_SERVICE:
            self.service = packet.payload
        elif packet.command == TRANSLATE_CRON:
            self.cron = packet.payload or True
        elif packet.command != TRANSLATE_LOCAL_ADDRESS:
            print("Invalid command:", packet.command)
        return False

    def absolute_uri(self, scheme=None, host=None, uri=None, query_string=None,
                     param=None):
        """Returns the absolute URI of this request.  You may override
        some of the attributes."""
        return beng_proxy.translation.uri.absolute_uri(self, scheme=scheme,
                                                       host=host, uri=uri,
                                                       query_string=query_string,
                                                       param=param)
