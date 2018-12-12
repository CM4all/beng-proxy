# The widget registry.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
import os.path
import sys
from twisted.internet import reactor, defer
from beng_proxy.translation.protocol import *
from beng_proxy.translation.response import Response
from beng_proxy.translation.dresponse import DeferredResponse

class MalformedLineError(Exception):
    def __init__(self, path, line):
        Exception.__init__(self, "Syntax error in %s: %s" % (path, line))
        self.path = path
        self.line = line

class _Lookup:
    def __init__(self, f):
        self._f = f
        self._response = DeferredResponse()
        self.d = defer.Deferred()

    def _handle_line(self, line):
        response = self._response

        m = re.match(r'^(?:untrusted|host)\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_UNTRUSTED, m.group(1))
            return
        m = re.match(r'^untrusted_prefix\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_UNTRUSTED_PREFIX, m.group(1))
            return
        m = re.match(r'^untrusted_site_suffix\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_UNTRUSTED_SITE_SUFFIX, m.group(1))
            return
        m = re.match(r'^untrusted_raw_site_suffix\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX, m.group(1))
            return
        m = re.match(r'^server\s+"(\S+)"$', line)
        if m:
            uri = m.group(1)
            return response.http(uri)
        m = re.match(r'^pipe\s+"(\S+)"', line)
        if m:
            line = line[4:]
            args = []
            line = re.sub(r'\s+"([^"]*)"', lambda m: args.append(m.group(1)), line)
            if not re.match(r'^\s*$', line):
                raise MalformedLineError(self.path, line)

            response.pipe(*args)
            return
        m = re.match(r'^cgi\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_CGI, m.group(1))
            return
        m = re.match(r'^fastcgi\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_FASTCGI, m.group(1))
            return
        m = re.match(r'^was\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_WAS, m.group(1))
            return
        m = re.match(r'^ajp\s+"(\S+)"\s+"(\S+)"$', line)
        if m:
            host, uri = m.group(1), m.group(2)
            return response.ajp(uri, host)
        m = re.match(r'^path\s+"(\S+)"$', line)
        if m:
            path = m.group(1)
            response.path(path)
            if path[-5:] == '.html' or path[-4:] == '.xml':
                response.content_type('text/html; charset=utf-8')
            return
        m = re.match(r'^script_name\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_SCRIPT_NAME, m.group(1))
            return
        m = re.match(r'^path_info\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_PATH_INFO, m.group(1))
            return
        m = re.match(r'^document_root\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_DOCUMENT_ROOT, m.group(1))
            return
        m = re.match(r'^action\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_ACTION, m.group(1))
            return
        m = re.match(r'^interpreter\s+"(\S+)"$', line)
        if m:
            response.packet(TRANSLATE_INTERPRETER, m.group(1))
            return
        m = re.match(r'^content_type\s+"([^\"]+)"$', line)
        if m:
            response.content_type(m.group(1))
            return
        m = re.match(r'^content_type_lookup\s+"([-._\w]+)"$', line)
        if m:
            response.packet(TRANSLATE_CONTENT_TYPE_LOOKUP, m.group(1))
            return
        m = re.match(r'^view\s+"([-_\w]+)"$', line)
        if m:
            response.view(m.group(1))
            return
        m = re.match(r'^cookie_host\s+"([-._\w]+)"$', line)
        if m:
            response.packet(TRANSLATE_COOKIE_HOST, m.group(1))
            return
        m = re.match(r'^local_uri\s+"(\S*/)"$', line)
        if m:
            response.packet(TRANSLATE_LOCAL_URI, m.group(1))
            return
        m = re.match(r'^group_container\s+"([-._\w]+)"$', line)
        if m:
            response.packet(TRANSLATE_GROUP_CONTAINER, m.group(1))
            return
        m = re.match(r'^group\s+"([-._\w]+)"$', line)
        if m:
            response.packet(TRANSLATE_WIDGET_GROUP, m.group(1))
            return

        m = re.match(r'^pair\s+"([^"]+)"\s+"([^"]*)"$', line)
        if m:
            name, value = m.group(1), m.group(2)
            response.pair(name, value)
            return

        if line == 'process':
            response.process()
        elif line == 'container':
            response.packet(TRANSLATE_CONTAINER)
        elif line == 'self_container':
            response.packet(TRANSLATE_SELF_CONTAINER)
        elif line == 'process_style':
            response.packet(TRANSLATE_PROCESS_STYLE)
        elif line == 'process_css':
            response.packet(TRANSLATE_PROCESS_CSS)
        elif line == 'process_text':
            response.packet(TRANSLATE_PROCESS_TEXT)
        elif line == 'prefix_css_class':
            response.packet(TRANSLATE_PREFIX_CSS_CLASS)
        elif line == 'prefix_xml_id':
            response.packet(TRANSLATE_PREFIX_XML_ID)
        elif line == 'direct_addressing':
            response.packet(TRANSLATE_DIRECT_ADDRESSING)
        elif line == 'stateful':
            response.packet(TRANSLATE_STATEFUL)
        elif line == 'filter':
            response.packet(TRANSLATE_FILTER)
        elif line == 'filter_4xx':
            response.packet(TRANSLATE_FILTER_4XX)
        elif line == 'sticky':
            response.packet(TRANSLATE_STICKY)
        elif line == 'dump_headers':
            response.packet(TRANSLATE_DUMP_HEADERS)
        elif line == 'focus_widget':
            response.packet(TRANSLATE_FOCUS_WIDGET)
        elif line == 'anchor_absolute':
            response.packet(TRANSLATE_ANCHOR_ABSOLUTE)
        elif line == 'widget_info':
            response.packet(TRANSLATE_WIDGET_INFO)
        else:
            raise MalformedLineError(self.path, line)

    def do(self):
        while True:
            line = self._f.readline()
            if len(line) == 0: break
            line = line.strip()
            if line == '' or line[0] == '#':
                continue
            try:
                d = self._handle_line(line)
                if d is not None:
                    d.addCallbacks(lambda result: self.do(), self.d.errback)
                    return
            except:
                self.d.errback(sys.exc_info()[0])
                return

        self.d.callback(self._response)

def _lookup(f):
    l = _Lookup(f)
    l.do()
    return l.d

class WidgetRegistry:
    def __init__(self, path):
        self._path = path

    def lookup(self, widget_type):
        path = os.path.join(self._path, widget_type)
        try:
            f = open(path)
        except IOError:
            return Response().status(404)

        return _lookup(f)
