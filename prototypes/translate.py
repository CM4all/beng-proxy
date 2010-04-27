#!/usr/bin/python
#
# Prototype for the beng-proxy translation server.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
import os.path
from twisted.internet import reactor
from twisted.internet.protocol import Protocol, Factory
from beng_proxy.translation import *

widgets_path = '/etc/cm4all/beng/widgets'
helpers_path = '/usr/bin'
cgi_path = '/usr/lib/cgi-bin'
demo_path = '/usr/share/cm4all/beng-proxy/demo/htdocs'
test_path = os.path.join(os.getcwd(), 'test')
coma_fastcgi = '/usr/bin/cm4all-coma-fastcgi'
ticket_fastcgi_dir = '/usr/lib/cm4all/ticket/cgi-bin'
ticket_database_uri = 'codb:sqlite:/tmp/ticket.sqlite'

cgi_re = re.compile(r'\.(?:sh|rb|py|pl|cgi)$')
php_re = re.compile(r'^(.*\.php\d*)((?:/.*)?)$')
coma_apps_re = re.compile(r'^/coma-apps/([-\w]+)/(\w+\.cls(?:/.*)?)$')

class Translation(Protocol):
    def connectionMade(self):
        print "Connected from", self.transport.client
        self._request = None
        self._packet = None

    def _handle_widget_lookup(self, widget_type, response):
        # checks on the name should be here.
        path = os.path.join(widgets_path, widget_type)
        try:
            f = open(path)
        except IOError:
            response.status(404)
            return

        for line in f.readlines():
            line = line.strip()
            if line == '' or line[0] == '#':
                continue
            m = re.match(r'^(?:untrusted|host)\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_UNTRUSTED, m.group(1))
                continue
            m = re.match(r'^server\s+"(\S+)"$', line)
            if m:
                uri = m.group(1)
                response.proxy(uri)
                continue
            m = re.match(r'^pipe\s+"(\S+)"', line)
            if m:
                line = line[4:]
                args = []
                line = re.sub(r'\s+"([^"]*)"', lambda m: args.append(m.group(1)), line)
                if not re.match(r'^\s*$', line):
                    print "Syntax error in %s: %s" % (path, line)
                    response.status(500)
                    break

                response.pipe(*args)
                continue
            m = re.match(r'^cgi\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_CGI, m.group(1))
                continue
            m = re.match(r'^fastcgi\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_FASTCGI, m.group(1))
                continue
            m = re.match(r'^ajp\s+"(\S+)"\s+"(\S+)"$', line)
            if m:
                host, uri = m.group(1), m.group(2)
                response.ajp(uri, host)
                continue
            m = re.match(r'^path\s+"(\S+)"$', line)
            if m:
                path = m.group(1)
                response.path(path)
                if path[-5:] == '.html':
                    response.content_type('text/html; charset=utf-8')
                continue
            m = re.match(r'^script_name\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_SCRIPT_NAME, m.group(1))
                continue
            m = re.match(r'^document_root\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_DOCUMENT_ROOT, m.group(1))
                continue
            m = re.match(r'^action\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_ACTION, m.group(1))
                continue
            m = re.match(r'^interpreter\s+"(\S+)"$', line)
            if m:
                response.packet(TRANSLATE_INTERPRETER, m.group(1))
                continue
            m = re.match(r'^content_type\s+"([^\"]+)"$', line)
            if m:
                response.content_type(m.group(1))
                continue
            m = re.match(r'^view\s+"([-_\w]+)"$', line)
            if m:
                response.view(m.group(1))
                continue

            if line == 'process':
                response.process()
            elif line == 'container':
                response.packet(TRANSLATE_CONTAINER)
            elif line == 'stateful':
                response.packet(TRANSLATE_STATEFUL)
            elif line == 'filter':
                response.packet(TRANSLATE_FILTER)
            else:
                print "Syntax error in %s: %s" % (path, line)
                response.status(500)
                break

    def _handle_local_file(self, path, response, delegate=False, jail=False):
        response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")

        cgi = cgi_re.search(path, 1)
        if cgi:
            response.packet(TRANSLATE_CGI, path)
            response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
            return

        m = php_re.match(path)
        if m:
            response.packet(TRANSLATE_FASTCGI, m.group(1))
            response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
            if jail:
                response.packet(TRANSLATE_ACTION, '/usr/bin/php-cgi5')
            else:
                response.packet(TRANSLATE_ACTION, '/usr/bin/php5-cgi')
            response.packet(TRANSLATE_PATH_INFO, m.group(2))
            if jail:
                response.packet(TRANSLATE_JAILCGI)
            return

        if path[-4:] == '.cls':
            response.packet(TRANSLATE_FASTCGI, path)
            response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
            if jail:
                response.packet(TRANSLATE_JAILCGI)
        else:
            response.path(path)
            if delegate and jail:
                response.delegate('/usr/bin/cm4all-beng-proxy-delegate-helper')
                # need another DOCUMENT_ROOT reference, for the
                # following JAILCGI packet
                response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
                response.packet(TRANSLATE_JAILCGI)
            elif delegate:
                response.delegate(os.path.join(helpers_path,
                                               'cm4all-beng-proxy-delegate-helper'))
            if path[-5:] == '.html':
                response.content_type('text/html; charset=utf-8')
                response.process(container=True)
            elif path[-4:] == '.txt':
                response.content_type('text/plain; charset=utf-8')
                response.gzipped(path + '.gz')

    def _handle_coma(self, response, base_uri, relative_uri, base_path,
                     config_file=None):
        i = relative_uri.find('/')
        if i < 0: i = len(relative_uri)
        relative_path, path_info = relative_uri[:i], relative_uri[i:]

        path = os.path.join(base_path, relative_path)
        response.packet(TRANSLATE_DOCUMENT_ROOT, base_path)
        response.packet(TRANSLATE_FASTCGI, path)
        response.packet(TRANSLATE_ACTION, coma_fastcgi)
        response.packet(TRANSLATE_PATH_INFO, path_info)
        if config_file is not None:
            response.pair('COMA_CONFIG_FILE', config_file)

        if path_info != '' and path_info[0] == '/':
            response.packet(TRANSLATE_BASE, base_uri + relative_path + '/')

    def _authenticate(self, authorization):
        if authorization is None: return False
        m = re.match(r'^Basic\s+(\S+)$', authorization)
        if m is None: return False
        x = m.group(1).decode('base64').split(':', 1)
        if len(x) != 2: return False
        username, password = x
        return username == 'hansi' and password == 'hansilein'

    def _handle_http(self, raw_uri, uri, authorization, response):
        if uri.find('/./') >= 0 or uri.find('/../') >= 0 or \
               uri[-2:] == '/.' or uri[-3:] == '/..' or \
               uri.find('//') >= 0 or uri.find('\0') >= 0:
            # these sequences are dangerous and may be used by
            # malicious users to attack our server
            response.status(400)
        elif uri[:19] == '/cm4all-beng-proxy/':
            from sys import argv
            if len(argv) >= 3:
                path = argv[2]
            else:
                from os.path import abspath, dirname, join
                path = join(dirname(dirname(abspath(argv[0]))), 'js/')
            self._handle_local_file(path + uri[19:], response)
        elif uri[:9] == '/cgi-bin/':
            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, uri[9:]))
        elif raw_uri[:11] == '/cfatest01/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[11:])
        elif raw_uri[:7] == '/proxy/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[7:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
        elif raw_uri[:8] == '/mangle/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[8:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
        elif raw_uri[:5] == '/ajp/':
            response.ajp(raw_uri[4:], 'cfatest01.intern.cm-ag:8009')
        elif uri[:8] == '/fcgi.rb':
            response.packet(TRANSLATE_FASTCGI, os.path.join(test_path, 'fcgi.rb'))
        elif uri == '/discard':
            response.packet(TRANSLATE_DISCARD_SESSION)
            response.status(204)
        elif uri[:10] == '/delegate/':
            self._handle_local_file('/var/www' + uri[9:], response, True)
        elif uri[:15] == '/jail-delegate/':
            self._handle_local_file('/home/www' + uri[14:], response, True, True)
        elif uri[:6] == '/jail/':
            self._handle_local_file('/home/www' + uri[5:], response, False, True)
        elif uri[:6] == '/demo/':
            self._handle_local_file(demo_path + uri[5:], response)
        elif uri[:6] == '/base/':
            response.packet(TRANSLATE_BASE, '/base/')
            response.packet(TRANSLATE_SCHEME, 'https')
            response.packet(TRANSLATE_HOST, 'xyz.intern.cm-ag')
            response.packet(TRANSLATE_URI, '/foo/' + uri[6:])
            self._handle_local_file('/var/www' + uri[5:], response)
        elif uri[:6] == '/coma/':
            self._handle_coma(response, uri[:6], uri[6:], '/home/max/svn/mod_coma/t/src')
        elif uri[:11] == '/coma-apps/':
            m = coma_apps_re.match(uri)
            if m:
                name, relative_uri = m.group(1), m.group(2)
                base_path = os.path.join('/usr/share/cm4all/coma/apps', name, 'htdocs')
                config_file = os.path.join('/etc/cm4all/coma/apps', name, 'coma.config')
                if not os.access(config_file, os.R_OK):
                    config_file = None
                self._handle_coma(response, uri[:11] + name + '/', relative_uri,
                                  base_path, config_file)
            else:
                response.status(404)
        elif uri[:16] == '/imageprocessor/':
            self._handle_coma(response, uri[16:],
                              '/usr/share/cm4all/coma/apps/imageprocessor/htdocs',
                              '/etc/cm4all/coma/apps/imageprocessor/coma.config')
        elif uri[:15] == '/ticket/create/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'create'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/create/')
            response.pair('TICKET_VAR', ticket_database_uri)
        elif uri[:15] == '/ticket/upload/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'upload'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/create/')
            response.pair('TICKET_VAR', ticket_database_uri)
        elif uri == '/filter':
            # two filters chained
            response.packet(TRANSLATE_DOCUMENT_ROOT, demo_path)
            response.path(os.path.join(demo_path, 'hello.txt'))
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe2.sed'))
        elif uri == '/redirect':
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/redirect/permanent':
            response.status(301)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/redirect/found':
            response.status(302)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/redirect/temporary':
            response.status(307)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/bounce':
            response.packet(TRANSLATE_BOUNCE, 'http://cfatest01.intern.cm-ag/test?uri=')
        elif uri[:6] == '/auth/':
            if self._authenticate(authorization):
                self._handle_local_file('/var/www' + uri[5:], response)
            else:
                response.packet(TRANSLATE_WWW_AUTHENTICATE, 'Basic realm="Demo"')
        elif uri[:8] == '/header/':
            response.header('X-Foo', 'Bar')
            self._handle_local_file('/var/www' + uri[7:], response)
        else:
            self._handle_local_file('/var/www' + uri, response)

        #response.packet(TRANSLATE_FILTER)
        # .... PROXY 'http://cfatest01.intern.cm-ag/filter.py'

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

        response = Response()
        if user is not None:
            response.packet(TRANSLATE_USER, user)
        if session is not None:
            response.packet(TRANSLATE_SESSION, session)

        if request.host == 'untrusted:8080':
            response.packet(TRANSLATE_UNTRUSTED, request.host)
        response.vary(TRANSLATE_HOST)

        if request.widget_type is not None:
            self._handle_widget_lookup(request.widget_type, response)

        if request.uri is not None:
            self._handle_http(request.raw_uri, request.uri, request.authorization, response)

        self.transport.write(response.finish())

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

    if argv[0].find('prototypes/') >= 0:
        # debug mode, run from svn working directory
        import os
        widgets_path = 'demo/widgets'
        helpers_path = os.path.join(os.getcwd(), 'src')
        cgi_path = os.path.join(os.getcwd(), 'demo/cgi-bin')
        demo_path = os.path.join(os.getcwd(), 'demo', 'htdocs')
        coma_fastcgi = os.path.join(os.getcwd(), '../../cgi-coma/src/cm4all-coma-fastcgi')
        ticket_fastcgi_dir = os.path.join(os.getcwd(), '../mod_ticket/src')

    if len(argv) >= 2:
        path = argv[1]
    else:
        path = '/tmp/beng-proxy-translate'
    reactor.listenUNIX(path, factory)
    reactor.run()
