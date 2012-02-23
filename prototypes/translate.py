#!/usr/bin/python
#
# Prototype for the beng-proxy translation server.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
import os.path
from twisted.python import log
from twisted.internet import reactor, defer
from twisted.internet.protocol import Protocol, Factory
from beng_proxy.translation import *
from beng_proxy.translation.widget import WidgetRegistry

widgets_path = '/etc/cm4all/beng/widgets'
helpers_path = '/usr/bin'
cgi_path = '/usr/lib/cgi-bin'
demo_path = '/usr/share/cm4all/beng-proxy/demo/htdocs'
test_path = os.path.join(os.getcwd(), 'test')
coma_fastcgi = '/usr/bin/cm4all-coma-fastcgi'
coma_was = '/usr/lib/cm4all/was/bin/coma-was'
image_processor_path = '/usr/share/cm4all/coma/apps/imageprocessor/htdocs'
ticket_fastcgi_dir = '/usr/lib/cm4all/ticket/cgi-bin'
ticket_database_uri = 'codb:sqlite:/tmp/ticket.sqlite'
xslt_fastcgi = '/usr/lib/cm4all/fcgi-bin/xslt'
xmlstrip = '/usr/lib/cm4all/was/bin/xmlstrip'
sed_fastcgi = '/usr/lib/cm4all/fcgi-bin/fsed'

cgi_re = re.compile(r'\.(?:sh|rb|py|pl|cgi)$')
php_re = re.compile(r'^(.*\.php\d*)((?:/.*)?)$')
coma_apps_re = re.compile(r'^/coma-apps/([-\w]+)/(\w+\.cls(?:/.*)?)$')

class Translation(Protocol):
    def connectionMade(self):
        log.msg("Connected from %s" % str(self.transport.client))
        self._request = None
        self._packet = None
        self.widget_registry = WidgetRegistry(widgets_path)

    def _handle_widget_lookup(self, widget_type):
        try:
            return self.widget_registry.lookup(widget_type)
        except:
            log.err()
            return Response().status(500)

    def _handle_local_file(self, path, response, delegate=False, jail=False, fastcgi=True, error_document=False):
        response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
        if error_document:
            response.packet(TRANSLATE_ERROR_DOCUMENT)

        cgi = cgi_re.search(path, 1)
        if cgi:
            response.packet(TRANSLATE_CGI, path)
            return

        m = php_re.match(path)
        if m:
            if fastcgi:
                response.packet(TRANSLATE_FASTCGI, m.group(1))
            else:
                response.packet(TRANSLATE_CGI, m.group(1))
            if jail:
                response.packet(TRANSLATE_ACTION, '/usr/bin/php-cgi5')
            else:
                response.packet(TRANSLATE_ACTION, '/usr/bin/php5-cgi')
            response.packet(TRANSLATE_PATH_INFO, m.group(2))
            if jail:
                response.packet(TRANSLATE_JAILCGI)
            response.packet(TRANSLATE_AUTO_BASE)
            return

        if path[-4:] == '.cls':
            response.packet(TRANSLATE_FASTCGI, path)
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
            if jail:
                response.packet(TRANSLATE_JAILCGI)
        else:
            response.path(path)
            if delegate and jail:
                response.delegate('/usr/bin/cm4all-beng-proxy-delegate-helper')
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
                     config_file=None, was=False):
        i = relative_uri.find('/')
        if i < 0: i = len(relative_uri)
        relative_path, path_info = relative_uri[:i], relative_uri[i:]

        path = os.path.join(base_path, relative_path)
        response.packet(TRANSLATE_DOCUMENT_ROOT, base_path)
        if was:
            response.packet(TRANSLATE_WAS, coma_was)
            response.pair('COMA_CLASS', path)
        else:
            response.packet(TRANSLATE_FASTCGI, path)
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
        response.packet(TRANSLATE_SCRIPT_NAME, base_uri + relative_path)
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

    def _handle_http(self, raw_uri, uri, authorization, check, response):
        if uri[:6] == '/site/':
            x = uri[6:]
            i = x.find('/')
            if i >= 0:
                response.packet(TRANSLATE_SITE, x[:i])
                raw_uri = uri = x[i:]

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
            i = uri.find('/', 9)
            if i > 0:
                script = uri[9:i]
                script_name = uri[:i]
                path_info = uri[i:]
            else:
                script = uri[9:]
                script_name = uri
                path_info = None

            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, script))
            response.packet(TRANSLATE_SCRIPT_NAME, script_name)
            if path_info is not None:
                response.packet(TRANSLATE_PATH_INFO, path_info)
                response.packet(TRANSLATE_AUTO_BASE)
        elif uri[:17] == '/cgi-transparent/':
            i = uri.find('/', 17)
            if i > 0:
                script = uri[17:i]
                script_name = uri[:i]
                path_info = uri[i:]
            else:
                script = uri[17:]
                script_name = uri
                path_info = None

            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, script))
            response.packet(TRANSLATE_SCRIPT_NAME, script_name)
            if path_info is not None:
                response.packet(TRANSLATE_PATH_INFO, path_info)
                response.packet(TRANSLATE_AUTO_BASE)

            response.packet(TRANSLATE_TRANSPARENT)
        elif raw_uri[:11] == '/cfatest01/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[11:])
        elif raw_uri[:13] == '/transparent/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[11:])
            response.packet(TRANSLATE_TRANSPARENT)
        elif raw_uri[:7] == '/proxy/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[7:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
        elif raw_uri[:8] == '/mangle/':
            response.proxy('http://cfatest01.intern.cm-ag/' + raw_uri[8:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
        elif raw_uri[:5] == '/ajp/':
            response.ajp('ajp://cfatest01.intern.cm-ag:8009' + raw_uri[4:], 'cfatest01.intern.cm-ag:8009')
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
        elif uri[:11] == '/jail-slow/':
            # execute PHP as CGI, not FastCGI
            self._handle_local_file('/home/www' + uri[10:], response, jail=True, fastcgi=False)
        elif uri[:11] == '/jail-home/':
            # execute PHP with DOCUMENT_ROOT below HOME
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www/htdocs')
            response.packet(TRANSLATE_FASTCGI, '/var/www/htdocs' + uri[10:])
            response.packet(TRANSLATE_ACTION, '/usr/bin/php-cgi5')
            response.packet(TRANSLATE_JAILCGI)
            response.packet(TRANSLATE_HOME, '/var/www')
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
        elif uri[:10] == '/coma-was/':
            self._handle_coma(response, uri[:10], uri[10:], '/home/max/svn/mod_coma/t/src', was=True)
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
            self._handle_coma(response, uri[:16], uri[16:],
                              image_processor_path,
                              '/etc/cm4all/coma/apps/imageprocessor/coma.config')
        elif uri[:20] == '/imageprocessor-was/':
            self._handle_coma(response, uri[:20], uri[20:],
                              image_processor_path,
                              '/etc/cm4all/coma/apps/imageprocessor/coma.config', was=True)
        elif uri[:23] == '/imageprocessor-filter/':
            uri = uri[22:]
            i = uri.rfind('.jpg/')
            if i < 0:
                response.status(404)
                return

            i = uri.index('/', i)
            uri, path_info = uri[:i], uri[i:]

            response.packet(TRANSLATE_BASE, "/imageprocessor-filter/")
            response.packet(TRANSLATE_REGEX, "^/imageprocessor-filter/(.+\.(?:jpe?g|png|gif|bmp))/([^/]+(?:/[^/])?)")
            response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
            response.path('/var/www' + uri)
            response.packet(TRANSLATE_EXPAND_PATH, r"/var/www/\1")
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(image_processor_path, 'filter.cls'))
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
            response.packet(TRANSLATE_PATH_INFO, path_info)
            response.packet(TRANSLATE_EXPAND_PATH_INFO, r"/\2")
        elif uri[:15] == '/ticket/create/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'create'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/create/')
            response.pair('TICKET_VAR', ticket_database_uri)
            response.pair('TICKET_MAX_SIZE', str(4*1024*1024))
        elif uri[:16] == '/ticket/create2/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'create'))
            response.packet(TRANSLATE_BASE, '/ticket/create2/')
            response.pair('TICKET_VAR', ticket_database_uri)
            response.pair('TICKET_URI', 'ftp://' + uri[16:])
        elif uri[:15] == '/ticket/upload/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'upload'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/upload/')
            response.pair('TICKET_VAR', ticket_database_uri)
        elif uri[:16] == '/ticket/control/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'control'))
            response.packet(TRANSLATE_PATH_INFO, uri[15:])
            response.packet(TRANSLATE_BASE, '/ticket/control/')
            response.pair('TICKET_VAR', ticket_database_uri)
        elif uri == '/ticket/cleanup':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'cleanup'))
            response.packet(TRANSLATE_PATH_INFO, uri[15:])
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
        elif uri == '/xslt':
            response.packet(TRANSLATE_FASTCGI, xslt_fastcgi)
            response.pair('STYLESHEET_PATH', os.path.join(demo_path, '../filter.xsl'))
            response.pair('DOCUMENT_PATH', os.path.join(demo_path, '../filter.xml'))
        elif uri == '/xslt-filter':
            response.path(os.path.join(demo_path, '../filter.xml'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, xslt_fastcgi)
            response.pair('STYLESHEET_PATH', os.path.join(demo_path, '../filter.xsl'))
        elif uri == '/xmlstrip':
            response.path(os.path.join(demo_path, 'xmlstrip2.html'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_WAS, xmlstrip)
        elif uri == '/sed':
            response.path(os.path.join(demo_path, 'xmlstrip2.html'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_PIPE, os.path.join(cgi_path, 'xmlstrip.sed'))
        elif uri == '/sed':
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
            response.pair('DOCUMENT_PATH', os.path.join(demo_path, 'hello.txt'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe2.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
        elif uri == '/sed-filter':
            response.path(os.path.join(demo_path, 'hello.txt'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe2.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
        elif uri == '/remote-sed':
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_ADDRESS_STRING, '/tmp/sed.socket')
            response.pair('DOCUMENT_PATH', os.path.join(demo_path, 'hello.txt'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe2.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
        elif uri == '/check':
            if check is None:
                response.packet(TRANSLATE_CHECK, 'ok')
                self._handle_local_file(os.path.join(demo_path, 'hello.txt'), response)
            elif check == 'ok':
                response.max_age(20)
                response.packet(TRANSLATE_PREVIOUS)
            else:
                # invalid request
                response.status(400)
        elif uri[:10] == '/balancer/':
            response.proxy('http://balancer/' + raw_uri[10:],
                           ('172.30.0.23:80', '172.30.0.23:8080'))
        elif uri[:8] == '/sticky/':
            response.proxy('http://sticky/' + raw_uri[8:],
                           ('172.30.0.23:80', '172.30.0.23:8080'))
            response.packet(TRANSLATE_STICKY)
        else:
            self._handle_local_file('/var/www' + uri, response,
                                    error_document=True)

        #response.packet(TRANSLATE_FILTER)
        # .... PROXY 'http://cfatest01.intern.cm-ag/filter.py'

    def _handle_request(self, request):
        if request.widget_type is not None:
            return self._handle_widget_lookup(request.widget_type)

        if request.error_document:
            log.msg("error %s %u" % (request.uri, request.status))
            return Response().path('/var/www/%u.html' % request.status).content_type('text/html')

        if request.session is not None: log.msg("- session = %s" % request.session)
        if request.param is not None: log.msg("- param = %s" % request.param)

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

        if request.host is not None and 'untrusted' in request.host:
            response.packet(TRANSLATE_UNTRUSTED, request.host)
        response.vary(TRANSLATE_HOST)

        if request.uri is not None:
            self._handle_http(request.raw_uri, request.uri, request.authorization,
                              request.check, response)

        return response

    def _success(self, result):
        self.transport.write(result.finish())
        self._request = None

    def _fail(self, fail):
        log.err(fail)
        self.transport.write(Response().status(500).finish())
        self._request = None

    def _handle_packet(self, packet):
        if packet.command == TRANSLATE_BEGIN:
            self._request = Request()
        elif self._request is not None:
            if self._request.packetReceived(packet):
                response = self._handle_request(self._request)
                if isinstance(response, defer.Deferred):
                    response.addCallbacks(self._success, self._fail)
                    return
                self._request = None
                self.transport.write(response.finish())
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

    if argv[0].find('prototypes/') >= 0:
        # debug mode, run from svn working directory
        import os
        widgets_path = 'demo/widgets'
        helpers_path = os.path.join(os.getcwd(), 'src')
        cgi_path = os.path.join(os.getcwd(), 'demo/cgi-bin')
        demo_path = os.path.join(os.getcwd(), 'demo', 'htdocs')

        src_dir = os.path.join(os.getenv('HOME'), 'git')
        if not os.path.isdir(os.path.join(src_dir, 'cgi-coma')):
            if os.path.isdir('../cgi-coma'):
                src_dir = os.path.join(os.getcwd(), '..')
            elif os.path.isdir('../../cgi-coma'):
                src_dir = os.path.join(os.getcwd(), '../..')

        coma_fastcgi = os.path.join(src_dir, 'cgi-coma/src/cm4all-coma-fastcgi')
        coma_was = os.path.join(src_dir, 'cgi-coma/src/coma-was')
        image_processor_path = os.path.join(src_dir, 'image-processor/src')
        ticket_fastcgi_dir = os.path.join(src_dir, 'mod_ticket/src')
        xslt_fastcgi = os.path.join(src_dir, 'filters/src/xslt')
        xmlstrip = os.path.join(src_dir, 'filters/src/xmlstrip')
        sed_fastcgi = os.path.join(src_dir, 'sed/sed/fsed')

    if len(argv) >= 2:
        path = argv[1]
    else:
        path = '/tmp/beng-proxy-translate'

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
