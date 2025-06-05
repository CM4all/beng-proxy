#
# Incremental request parser.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

import six
import array, struct
from urllib.parse import unquote

from .protocol import *
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
        self.check_header = None
        self.auth = None
        self.http_auth = None
        self.token_auth = None
        self.auth_token = None
        self.mount_listen_stream = None
        self.recover_session = None
        self.want_full_uri = None
        self.chain = None
        self.chain_header = None
        self.param = None
        self.layout = None
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
        self.path_exists = None
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
        self.plan = None
        self.cron = False
        self.execute = False
        self.base = None
        self.regex = None

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
                # this "struct" kludge is for Python 2/3 compatibility
                self.protocol_version = struct.unpack('B', packet.payload[:1])[0]
        elif packet.command == TRANSLATE_END:
            return True
        elif packet.command == TRANSLATE_HOST:
            self.host = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_ALT_HOST:
            self.alt_host = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_URI:
            self.raw_uri = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_ARGS:
            self.args = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_QUERY_STRING:
            self.query_string = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_WIDGET_TYPE:
            self.widget_type = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_SESSION:
            self.session = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_CHECK:
            self.check = packet.payload
        elif packet.command == TRANSLATE_CHECK_HEADER:
            self.check_header = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_AUTH:
            self.auth = packet.payload
        elif packet.command == TRANSLATE_HTTP_AUTH:
            self.http_auth = packet.payload
        elif packet.command == TRANSLATE_TOKEN_AUTH:
            self.token_auth = packet.payload
        elif packet.command == TRANSLATE_AUTH_TOKEN:
            self.auth_token = packet.payload
        elif packet.command == TRANSLATE_MOUNT_LISTEN_STREAM:
            self.mount_listen_stream = packet.payload
        elif packet.command == TRANSLATE_RECOVER_SESSION:
            self.recover_session = packet.payload
        elif packet.command == TRANSLATE_WANT_FULL_URI:
            self.want_full_uri = packet.payload
        elif packet.command == TRANSLATE_CHAIN:
            self.chain = packet.payload
        elif packet.command == TRANSLATE_CHAIN_HEADER:
            self.chain_header = packet.payload
        elif packet.command == TRANSLATE_PARAM:
            self.param = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_LAYOUT:
            self.layout = packet.payload
        elif packet.command == TRANSLATE_LISTENER_TAG:
            self.listener_tag = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_LOCAL_ADDRESS_STRING:
            self.local_address = packet.payload.decode('ascii')
            self.local_port = _parse_port(self.local_address)
        elif packet.command == TRANSLATE_REMOTE_HOST:
            self.remote_host = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_USER_AGENT:
            self.user_agent = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_LANGUAGE:
            self.accept_language = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_AUTHORIZATION:
            self.authorization = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_STATUS:
            if len(packet.payload) == 2:
                self.status = struct.unpack('H', packet.payload)[0]
        elif packet.command == TRANSLATE_WANT:
            self.want = array.array('H')
            if six.PY2:
                self.want.fromstring(packet.payload)
            else:
                self.want.frombytes(packet.payload)
        elif packet.command == TRANSLATE_FILE_NOT_FOUND:
            self.file_not_found = packet.payload
        elif packet.command == TRANSLATE_PATH_EXISTS:
            self.path_exists = packet.payload
        elif packet.command == TRANSLATE_DIRECTORY_INDEX:
            self.directory_index = packet.payload
        elif packet.command == TRANSLATE_INTERNAL_REDIRECT:
            self.internal_redirect = packet.payload
        elif packet.command == TRANSLATE_ENOTDIR:
            self.enotdir = packet.payload
        elif packet.command == TRANSLATE_CONTENT_TYPE_LOOKUP:
            self.content_type_lookup = packet.payload
        elif packet.command == TRANSLATE_SUFFIX:
            self.suffix = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_ERROR_DOCUMENT:
            self.error_document = True
            self.error_document_payload = packet.payload
        elif packet.command == TRANSLATE_PROBE_PATH_SUFFIXES:
            self.probe_path_suffixes = packet.payload
        elif packet.command == TRANSLATE_PROBE_SUFFIX:
            self.probe_suffix = packet.payload.decode('utf-8')
        elif packet.command == TRANSLATE_READ_FILE:
            self.read_file = packet.payload
        elif packet.command == TRANSLATE_POOL:
            self.pool = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_USER:
            self.user = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_LOGIN:
            self.login = True
        elif packet.command == TRANSLATE_PASSWORD:
            self.password = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_SERVICE:
            self.service = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_PLAN:
            self.plan = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_CRON:
            if packet.payload:
                self.cron = packet.payload.decode('ascii')
            else:
                self.cron = True
        elif packet.command == TRANSLATE_EXECUTE:
            if packet.payload:
                self.execute = packet.payload.decode('ascii')
            else:
                self.execute = True
        elif packet.command == TRANSLATE_BASE:
            self.base = packet.payload.decode('ascii')
        elif packet.command == TRANSLATE_REGEX:
            self.regex = packet.payload.decode('ascii')
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
