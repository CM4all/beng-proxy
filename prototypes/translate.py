#!/usr/bin/python
#
# Prototype for the beng-proxy translation server.
#
# Author: Max Kellermann <mk@cm4all.com>

import re
import os
import urllib
from twisted.python import log
from twisted.internet import reactor, defer
from twisted.internet.protocol import Protocol, Factory
from beng_proxy.translation import *
from beng_proxy.translation.widget import WidgetRegistry

widgets_path = '/etc/cm4all/beng/widgets'
default_helpers_path = '/usr/lib/cm4all/beng-proxy/delegate/bin'
helpers_path = default_helpers_path
cgi_path = '/usr/lib/cgi-bin'
was_path = '/usr/lib/cm4all/was/bin'
demo_path = '/usr/share/cm4all/beng-proxy/demo/htdocs'
test_path = os.path.join(os.getcwd(), 'test')
was_examples_path = was_path
was_examples = ['hello', 'random', 'mirror']
coma_fastcgi = '/usr/bin/cm4all-coma-fastcgi'
coma_was = os.path.join(was_path, 'coma-was')
coma_demo = '/var/www'
image_processor_path = '/usr/share/cm4all/coma/apps/imageprocessor/htdocs'
ticket_fastcgi_dir = '/usr/lib/cm4all/ticket/cgi-bin'
ticket_database_uri = 'codb:sqlite:/tmp/ticket.sqlite'
xslt_fastcgi = '/usr/lib/cm4all/fcgi-bin/xslt'
xmlstrip = os.path.join(was_path, 'xmlstrip')
sed_fastcgi = '/usr/lib/cm4all/fcgi-bin/fsed'
apache_lhttpd = '/usr/lib/cm4all/lhttp/bin/apache-lhttpd'

davos_plain = os.path.join(was_path, 'davos-plain')
davos_od = os.path.join(was_path, 'davos-od')
od_conf = '/etc/cm4all/davos/od.conf'
od_section = 'test'

cgi_re = re.compile(r'\.(?:sh|rb|py|pl|cgi)$')
php_re = re.compile(r'^(.*\.php\d*)((?:/.*)?)$')
coma_apps_re = re.compile(r'^/coma-apps/([-\w]+)/(\w+\.cls(?:/.*)?)$')

content_types = {
    'html': 'text/html',
    'txt': 'text/plain',
    'jpg': 'image/jpeg',
    'jpeg': 'image/jpeg',
    'png': 'image/png',
}

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
            return Response(protocol_version=2).status(500)

    def _handle_content_type_lookup(self, payload, suffix):
        log.msg("content_type_lookup '%s' suffix='%s'" % (payload, suffix))

        response = Response(protocol_version=2)
        if suffix == 'phtml':
            response.packet(TRANSLATE_CONTENT_TYPE, 'text/html')
            response.process(container=True)
            return response

        if suffix in content_types:
            response.packet(TRANSLATE_CONTENT_TYPE, content_types[suffix])
        return response

    def _handle_login(self, user, password, service, listener_tag):
        log.msg("login %s password=%s service=%s tag=%s" % (repr(user), repr(password), repr(service), repr(listener_tag)))

        response = Response(protocol_version=2)
        if user is None or not re.match(r'^[-_\w]+$', user):
            response.status(400)
            return response

        if password is not None and password != 'password':
            response.status(403)
            return response

        response.packet(TRANSLATE_USER_NAMESPACE)
        response.packet(TRANSLATE_PID_NAMESPACE_NAME, user)
        response.packet(TRANSLATE_HOME, os.path.join('/var/www', user))
        response.packet(TRANSLATE_TOKEN, 'xyz')
        response.uid_gid(500, 100)
        if service != 'ftp':
            if service == 'sftp':
                response.packet(TRANSLATE_MOUNT_ROOT_TMPFS)
            else:
                response.packet(TRANSLATE_PIVOT_ROOT, '/srv/chroot/jessie')
                response.packet(TRANSLATE_MOUNT_PROC)
                response.packet(TRANSLATE_MOUNT_TMP_TMPFS)
            response.packet(TRANSLATE_MOUNT_HOME, '/home')
            response.packet(TRANSLATE_UTS_NAMESPACE, 'host-' + user)
        return response

    def _handle_cron(self, partition, tag, user, uri, param):
        log.msg("cron partition=%s tag=%s user=%s uri=%s param=%s" % (repr(partition), repr(tag), repr(user), repr(uri), repr(param)))

        response = Response(protocol_version=2)
        if user is None or not re.match(r'^[-_\w]+$', user):
            response.status(400)
            return response

        if uri is not None:
            response.packet(TRANSLATE_EXECUTE, '/bin/echo')
            response.packet(TRANSLATE_APPEND, 'Hello, World!')
            response.packet(TRANSLATE_APPEND, uri)

        response.packet(TRANSLATE_HOME, os.path.join('/var/www', user))
        response.packet(TRANSLATE_USER_NAMESPACE)
        response.packet(TRANSLATE_PID_NAMESPACE)
        #response.uid_gid(500, 100)
        response.packet(TRANSLATE_PIVOT_ROOT, '/srv/chroot/jessie')
        response.packet(TRANSLATE_MOUNT_HOME, '/home')
        response.packet(TRANSLATE_MOUNT_PROC)
        response.packet(TRANSLATE_MOUNT_TMP_TMPFS)
        response.packet(TRANSLATE_UTS_NAMESPACE, 'host-' + user)
        return response

    def _handle_auth(self, auth, uri, session, alt_host):
        log.msg("auth '%s' uri='%s' session='%s' alt_host='%s'" % (auth, uri, session, alt_host))

        response = Response(protocol_version=2)
        response.max_age(0)
        response.packet(TRANSLATE_USER, 'hans')
        response.max_age(20)
        return response

    def _handle_pool(self, name, listener_tag, host):
        log.msg("pool '%s' tag=%s host=%s" % (name, repr(listener_tag), repr(host)))

        response = Response(protocol_version=2)

        if host == '404':
            response.status(404)
            response.message(listener_tag or 'no LISTENER_TAG')
            response.vary(TRANSLATE_HOST, TRANSLATE_LISTENER_TAG)
        elif host == '403':
            response.status(403)
            response.vary(TRANSLATE_HOST)
        else:
            response.packet(TRANSLATE_POOL, name + '_')
            response.packet(TRANSLATE_SITE, host)
            response.vary(TRANSLATE_HOST)

        return response

    def _handle_hosting(self, request, response, base, uri,
                        document_root='/var/www'):
        response.packet(TRANSLATE_BASE, base)
        response.packet(TRANSLATE_DOCUMENT_ROOT, document_root)

        path = os.path.join(document_root, urllib.unquote(uri))
        easy_path = document_root + '/'

        if request.file_not_found == '404':
            response.status(404)
        elif request.file_not_found == 'index.html':
            # index.html does not exist, fall back to directory_index.py
            response.packet(TRANSLATE_REGEX, r'^(.*)$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'directory_index.py'))
            response.packet(TRANSLATE_PATH_INFO, os.path.join(cgi_path, 'directory_index.py'))
            response.pair('DIRECTORY', '/dummy')
            response.packet(TRANSLATE_EXPAND_PAIR, r'DIRECTORY=%s/\1' % document_root)
        elif request.directory_index:
            # directory without trailing slash: redirect with slash appended
            response.packet(TRANSLATE_REGEX, r'^(.*)$')
            response.packet(TRANSLATE_REDIRECT, 'dummy')
            response.packet(TRANSLATE_EXPAND_REDIRECT, r'\1/')
        elif uri[-4:] == '.cls':
            # run COMA-FastCGI
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_REGEX, r'^(.*\.cls)$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_FASTCGI, easy_path)
            response.packet(TRANSLATE_EXPAND_PATH, document_root + r'/\1')
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
            response.pair('UPLOAD_BUFFER_SIZE', '4M')
            response.packet(TRANSLATE_FILE_NOT_FOUND, '404')
            response.packet(TRANSLATE_ENOTDIR, 'foo')
        elif uri[-4:] == '.php':
            # run PHP-FastCGI
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_REGEX, r'^(.*\.php)$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_FASTCGI, easy_path)
            response.packet(TRANSLATE_EXPAND_PATH, document_root + r'/\1')
            response.packet(TRANSLATE_ACTION, '/usr/bin/php5-cgi')
            response.packet(TRANSLATE_FILE_NOT_FOUND, '404')
            response.packet(TRANSLATE_ENOTDIR, 'foo')
        elif uri[-3:] == '.py':
            # run Python-CGI
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_REGEX, r'^(.*\.py)$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_CGI, easy_path)
            response.packet(TRANSLATE_EXPAND_PATH, document_root + r'/\1')
            response.packet(TRANSLATE_INTERPRETER, '/usr/bin/python')
            response.packet(TRANSLATE_FILE_NOT_FOUND, '404')
            response.packet(TRANSLATE_ENOTDIR, 'foo')
        elif uri == '' or uri[-1] == '/':
            # deliver index.html with fallback (via TRANSLATE_FILE_NOT_FOUND)
            response.packet(TRANSLATE_REGEX, r'^(.*/)?$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.path('/dummy')
            response.packet(TRANSLATE_EXPAND_PATH, r"/var/www/\1index.html")
            response.packet(TRANSLATE_FILE_NOT_FOUND, 'index.html')
            response.packet(TRANSLATE_ENOTDIR, 'foo')
        else:
            # static file
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_INVERSE_REGEX, r'(\.(cls|php|py)|/)$')
            response.packet(TRANSLATE_PATH, easy_path)
            response.packet(TRANSLATE_AUTO_GZIPPED)
            response.packet(TRANSLATE_ENOTDIR, 'foo')
            response.packet(TRANSLATE_DIRECTORY_INDEX, 'foo')

    def _handle_probe(self, request, response, base, uri,
                      document_root='/var/www'):
        response.packet(TRANSLATE_BASE, base)
        response.packet(TRANSLATE_DOCUMENT_ROOT, document_root)

        if request.probe_path_suffixes is None:
            response.packet(TRANSLATE_REGEX, r'^(.*)$')
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_EXPAND_TEST_PATH, document_root + r'/\1')
            response.packet(TRANSLATE_PROBE_PATH_SUFFIXES, 'pps')
            response.packet(TRANSLATE_PROBE_SUFFIX, '.py')
            response.packet(TRANSLATE_PROBE_SUFFIX, '.php')
            response.packet(TRANSLATE_PROBE_SUFFIX, '.cls')
            response.packet(TRANSLATE_PROBE_SUFFIX, '.html')
            response.packet(TRANSLATE_PROBE_SUFFIX, '.txt')
        else:
            if request.probe_suffix is None:
                response.status(404)
            else:
                response.packet(TRANSLATE_REGEX, r'^(.*)$')
                response.packet(TRANSLATE_REGEX_TAIL)

                path = document_root + uri + request.probe_suffix
                if request.probe_suffix == '.py':
                    response.packet(TRANSLATE_CGI, path)
                    response.packet(TRANSLATE_INTERPRETER, '/usr/bin/python')
                elif request.probe_suffix == '.php':
                    response.packet(TRANSLATE_FASTCGI, path)
                    response.packet(TRANSLATE_ACTION, '/usr/bin/php5-cgi')
                elif request.probe_suffix == '.cls':
                    response.packet(TRANSLATE_FASTCGI, path)
                    response.packet(TRANSLATE_ACTION, coma_fastcgi)
                else:
                    response.packet(TRANSLATE_PATH, path)
                response.packet(TRANSLATE_EXPAND_PATH, document_root + r'/\1' + request.probe_suffix)

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
            response.pair('UPLOAD_BUFFER_SIZE', '4M')
            if jail:
                response.packet(TRANSLATE_JAILCGI)
        else:
            response.path(path)
            response.expires_relative(1800)
            if delegate and jail:
                response.delegate(os.path.join(default_helpers_path,
                                               'delegate-helper'))
                response.packet(TRANSLATE_JAILCGI)
            elif delegate:
                response.delegate(os.path.join(helpers_path,
                                               'delegate-helper'))
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
            response.packet(TRANSLATE_NO_NEW_PRIVS)
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

    def _handle_http(self, request, raw_uri, uri, authorization,
                     check, want_full_uri, want, file_not_found, directory_index,
                     ua_class, response):
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
        elif raw_uri[:6] == '/1234/':
            response.http('http://localhost:1234/' + raw_uri[4:])
        elif raw_uri[:11] == '/cfatest01/':
            response.http('http://cfatest01.intern.cm-ag/' + raw_uri[11:])
        elif raw_uri[:13] == '/transparent/':
            response.http('http://cfatest01.intern.cm-ag/' + raw_uri[11:])
            response.packet(TRANSLATE_TRANSPARENT)
        elif raw_uri[:7] == '/proxy/':
            response.http('http://cfatest01.intern.cm-ag/' + raw_uri[7:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_YES))
        elif raw_uri[:8] == '/mangle/':
            response.http('http://cfatest01.intern.cm-ag/' + raw_uri[8:])
            response.request_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
            response.response_header_forward((HEADER_GROUP_ALL, HEADER_FORWARD_MANGLE))
        elif raw_uri[:10] == '/relocate/':
            response.packet(TRANSLATE_BASE, '/relocate/')
            response.packet(TRANSLATE_EASY_BASE)
            response.http('http://cfatest01.intern.cm-ag/')
            response.response_header_forward((HEADER_GROUP_LINK, HEADER_FORWARD_MANGLE))
        elif raw_uri[:24] == '/vary-user-agent/remote/':
            if want is None or TRANSLATE_USER_AGENT not in want:
                response.want(TRANSLATE_USER_AGENT)
                return

            response.vary(TRANSLATE_USER_AGENT)
            response.http('http://cfatest01.intern.cm-ag/' + raw_uri[24:])
        elif raw_uri[:5] == '/nfs/':
            response.nfs('172.28.0.8', '/srv/nfs4/foo', raw_uri[4:])
        elif uri[:8] == '/fcgi.rb':
            response.packet(TRANSLATE_FASTCGI, os.path.join(test_path, 'fcgi.rb'))
        elif uri == '/discard':
            response.packet(TRANSLATE_DISCARD_SESSION)
            response.status(204)
        elif uri in ['/null', '/zero', '/urandom']:
            response.path('/dev' + uri)
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
        elif uri[:11] == '/easy-base/':
            response.packet(TRANSLATE_BASE, '/easy-base/')
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_SCHEME, 'https')
            response.packet(TRANSLATE_HOST, 'xyz.intern.cm-ag')
            response.path('/var/www/')
        elif uri[:8] == '/nsinfo/':
            user_ns = pid_ns = False
            pivot_root = None
            for x in uri[8:]:
                if x == 'U':
                    user_ns = True
                elif x == 'p':
                    pid_ns = True
                elif x == 'm':
                    pivot_root = '/srv/chroot/jessie'
                else:
                    raise Exception('Unknown namespace: "%c"' % x)

            home = cgi_path

            if pivot_root is not None:
                home = '/home'

            response.packet(TRANSLATE_CGI, os.path.join(home, 'nsinfo.sh'))
            response.packet(TRANSLATE_HOME, cgi_path)

            if pivot_root is not None:
                response.packet(TRANSLATE_PIVOT_ROOT, pivot_root)
                response.packet(TRANSLATE_MOUNT_HOME, '/home')
                response.packet(TRANSLATE_MOUNT_TMP_TMPFS)

            if user_ns:
                response.packet(TRANSLATE_USER_NAMESPACE)

            if pid_ns:
                response.packet(TRANSLATE_PID_NAMESPACE)

            if pid_ns or pivot_root is not None:
                response.packet(TRANSLATE_MOUNT_PROC)

        elif uri[:6] == '/coma/':
            self._handle_coma(response, uri[:6], uri[6:], coma_demo)
        elif uri[:10] == '/coma-was/':
            self._handle_coma(response, uri[:10], uri[10:], coma_demo, was=True)
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
            response.packet(TRANSLATE_REGEX, "^(.+\.(?:jpe?g|png|gif|bmp))/([^/]+(?:/[^/])?)")
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_DOCUMENT_ROOT, "/var/www")
            response.path('/var/www' + uri)
            response.packet(TRANSLATE_EXPAND_PATH, r"/var/www/\1")
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(image_processor_path, 'filter.cls'))
            response.packet(TRANSLATE_ACTION, coma_fastcgi)
            response.packet(TRANSLATE_PATH_INFO, path_info)
            response.packet(TRANSLATE_EXPAND_PATH_INFO, r"/\2")
        elif uri == '/lhttp/':
            response.packet(TRANSLATE_LHTTP_PATH, os.path.join(test_path, 'run_http_server'))
            response.packet(TRANSLATE_APPEND, 'accept')
            response.packet(TRANSLATE_APPEND, '0')
            response.packet(TRANSLATE_APPEND, 'fixed')
            response.packet(TRANSLATE_LHTTP_URI, uri)
            response.packet(TRANSLATE_CONCURRENCY, '\x04\x00')
        elif uri == '/lhttp/mirror':
            response.packet(TRANSLATE_LHTTP_PATH, os.path.join(test_path, 'run_http_server'))
            response.packet(TRANSLATE_APPEND, 'accept')
            response.packet(TRANSLATE_APPEND, '0')
            response.packet(TRANSLATE_APPEND, 'mirror')
            response.packet(TRANSLATE_LHTTP_URI, uri)
            response.packet(TRANSLATE_CONCURRENCY, '\x04\x00')
        elif uri == '/apache/':
            response.packet(TRANSLATE_LHTTP_PATH, apache_lhttpd)
            response.packet(TRANSLATE_APPEND, '-DFOREGROUND')
            response.packet(TRANSLATE_LHTTP_URI, uri[7:])
            response.packet(TRANSLATE_LHTTP_HOST, 'localhost:80')
            response.packet(TRANSLATE_CONCURRENCY, '\x04\x00')
        elif uri[:15] == '/ticket/create/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'create'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/create/')
            response.pair('TICKET_VAR', ticket_database_uri)
            response.pair('TICKET_MAX_SIZE', str(4*1024*1024))
        elif uri[:16] == '/ticket/create2/':
            response.packet(TRANSLATE_BASE, '/ticket/create2/')
            response.packet(TRANSLATE_REGEX, "^(.*)$")
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'create'))
            response.pair('TICKET_VAR', ticket_database_uri)
            response.pair('TICKET_URI', 'ftp://' + uri[16:])
            response.packet(TRANSLATE_EXPAND_PAIR, r'TICKET_URI=ftp://\1')
        elif uri[:15] == '/ticket/upload/':
            response.packet(TRANSLATE_FASTCGI, os.path.join(ticket_fastcgi_dir,
                                                            'upload'))
            response.packet(TRANSLATE_DOCUMENT_ROOT, '/var/www')
            response.packet(TRANSLATE_PATH_INFO, uri[14:])
            response.packet(TRANSLATE_BASE, '/ticket/upload/')
            response.pair('TICKET_VAR', ticket_database_uri)
            #response.pair('TICKET_FTP_USE_EPSV', '0')
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
        elif uri == '/redirect/query_string':
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
            response.packet(TRANSLATE_REDIRECT_QUERY_STRING)
        elif uri == '/redirect/permanent':
            response.status(301)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/redirect/found':
            response.status(302)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri == '/redirect/temporary':
            response.status(307)
            response.packet(TRANSLATE_REDIRECT, 'http://cfatest01.intern.cm-ag/')
        elif uri[:19] == '/redirect/full-uri/':
            response.packet(TRANSLATE_BASE, uri[:19])
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_REDIRECT, 'http://other.server/foo/')
            response.packet(TRANSLATE_REDIRECT_QUERY_STRING)
            response.packet(TRANSLATE_REDIRECT_FULL_URI)
        elif uri == '/bounce':
            response.packet(TRANSLATE_BOUNCE, 'http://cfatest01.intern.cm-ag/test?uri=')
        elif uri[:6] == '/auth/':
            if self._authenticate(authorization):
                self._handle_local_file('/var/www' + uri[5:], response)
            else:
                response.packet(TRANSLATE_WWW_AUTHENTICATE, 'Basic realm="Demo"')
        elif raw_uri[:7] == '/auth2/':
            response.packet(TRANSLATE_AUTH, 'dummy')
            self._handle_hosting(request, response, '/auth2/', raw_uri[7:])
        elif uri[:8] == '/header/':
            response.header('X-Foo', 'Bar')
            self._handle_local_file('/var/www' + uri[7:], response)
        elif uri[:5] == '/was/':
            name = uri[5:]
            if name in was_examples:
                response.packet(TRANSLATE_WAS, os.path.join(was_examples_path, name))
            else:
                response.status(404)
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
        elif uri[:11] == '/filter4xx/':
            response.path(os.path.join('/var/www', uri[11:]))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'underscore.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
            response.packet(TRANSLATE_FILTER_4XX)
        elif uri == '/remote-sed':
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_ADDRESS_STRING, '/tmp/sed.socket')
            response.pair('DOCUMENT_PATH', os.path.join(demo_path, 'hello.txt'))
            response.packet(TRANSLATE_FILTER)
            response.packet(TRANSLATE_FASTCGI, os.path.join(cgi_path, 'pipe2.sed'))
            response.packet(TRANSLATE_ACTION, sed_fastcgi)
        elif uri == '/subst':
            response.path(os.path.join(demo_path, 'subst.txt'))
            response.packet(TRANSLATE_SUBST_YAML_FILE, '\0' + os.path.join(demo_path, 'subst.yaml') + '\0')
        elif uri == '/subst2':
            response.path(os.path.join(demo_path, 'subst.txt'))
            response.packet(TRANSLATE_SUBST_YAML_FILE, '\0' + os.path.join(demo_path, 'subst.yaml') + '\0child')
        elif uri == '/subst-alt':
            response.path(os.path.join(demo_path, 'subst-alt.txt'))
            response.packet(TRANSLATE_SUBST_YAML_FILE, '\0' + os.path.join(demo_path, 'subst.yaml') + '\0')
            response.packet(TRANSLATE_SUBST_ALT_SYNTAX)
        elif uri == '/subst2-alt':
            response.path(os.path.join(demo_path, 'subst-alt.txt'))
            response.packet(TRANSLATE_SUBST_YAML_FILE, '\0' + os.path.join(demo_path, 'subst.yaml') + '\0child')
            response.packet(TRANSLATE_SUBST_ALT_SYNTAX)
        elif uri == '/dav':
            response.packet(TRANSLATE_WAS, davos_plain)
            response.pair('DAVOS_DOCUMENT_ROOT', '/var/www')
            response.pair('DAVOS_MOUNT', '/dav/')
            response.pair('DAVOS_DAV_HEADER', '3')
            response.request_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
        elif uri[:5] == '/dav/':
            response.packet(TRANSLATE_BASE, "/dav/")
            response.packet(TRANSLATE_WAS, davos_plain)
            response.pair('DAVOS_DOCUMENT_ROOT', '/var/www')
            response.pair('DAVOS_MOUNT', '/dav/')
            response.request_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
        elif uri[:8] == '/dav-od/':
            uri = uri[8:]
            i = uri.find('/')
            if i > 0:
                site = uri[:i]
                response.packet(TRANSLATE_BASE, '/dav-od/' + site + '/')
            elif i < 0:
                response.packet(TRANSLATE_BASE, "/dav-od/")
                if len(uri) == 0:
                    response.status(404)
                    return
                site = uri
            else:
                response.status(404)
                return

            response.packet(TRANSLATE_REGEX, "^/dav-od/([^/]+)")
            response.packet(TRANSLATE_WAS, davos_od)
            response.packet(TRANSLATE_APPEND, od_conf)
            response.packet(TRANSLATE_APPEND, od_section)
            response.pair('DAVOS_MOUNT', '/dav-od/' + site + '/')
            response.packet(TRANSLATE_EXPAND_PAIR, r'DAVOS_MOUNT=/dav-od/\1/')
            response.pair('DAVOS_SITE', site)
            response.packet(TRANSLATE_EXPAND_PAIR, r'DAVOS_SITE=\1')
            response.request_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
            response.response_header_forward((HEADER_GROUP_OTHER, HEADER_FORWARD_YES))
        elif uri == '/validate_mtime':
            response.path(os.path.join(demo_path, 'hello.txt'))
            stamp_path = '/tmp/stamp'
            response.validate_mtime(os.stat(stamp_path).st_mtime, stamp_path)
        elif uri == '/per_host/invalidate':
            response.path(os.path.join(demo_path, 'hello.txt'))
            response.invalidate(TRANSLATE_HOST)
            response.max_age(0)
        elif uri[:10] == '/per_host/':
            response.path(os.path.join(demo_path, 'hello.txt'))
            response.vary(TRANSLATE_HOST)
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
        elif uri[:14] == '/want_full_uri':
            if want_full_uri is None:
                response.packet(TRANSLATE_WANT_FULL_URI, 'foo')
            elif want_full_uri == 'foo':
                response.max_age(20)
                response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'env.py'))
                response.packet(TRANSLATE_SCRIPT_NAME, uri)
            else:
                # invalid request
                response.status(400)
        elif uri[:10] == '/balancer/':
            response.http('http://balancer/' + raw_uri[10:],
                          ('172.30.0.23:80', '172.30.0.23:8080'))
        elif uri[:8] == '/sticky/':
            response.http('http://sticky/' + raw_uri[8:],
                          ('172.30.0.23:80', '172.30.0.23:8080'))
            response.packet(TRANSLATE_STICKY)
        elif raw_uri[:23] == '/vary-user-agent/local/':
            if want is None or TRANSLATE_USER_AGENT not in want:
                response.want(TRANSLATE_USER_AGENT)
                return

            response.vary(TRANSLATE_USER_AGENT)
            self._handle_local_file('/var/www' + uri[22:], response,
                                    error_document=True)
        elif raw_uri == '/ua_class':
            if want is None or TRANSLATE_UA_CLASS not in want:
                response.want(TRANSLATE_UA_CLASS)
                return

            response.vary(TRANSLATE_UA_CLASS, TRANSLATE_WANT)
            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'env.py'))
            response.packet(TRANSLATE_SCRIPT_NAME, uri)
            if ua_class is not None:
                response.packet(TRANSLATE_PATH_INFO, ua_class)
        elif raw_uri == '/listener_tag':
            if want is None or TRANSLATE_LISTENER_TAG not in want:
                response.want(TRANSLATE_LISTENER_TAG)
                return

            response.vary(TRANSLATE_LISTENER_TAG)
            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'env.py'))
            response.packet(TRANSLATE_SCRIPT_NAME, uri)
            if request.listener_tag is not None:
                response.packet(TRANSLATE_PATH_INFO, request.listener_tag)
        elif uri == '/want_user':
            if want is None or TRANSLATE_USER not in want:
                response.want(TRANSLATE_USER)
                return

            response.vary(TRANSLATE_USER)
            response.packet(TRANSLATE_AUTH, 'dummy')
            if request.user is None:
                response.status(403)
            else:
                response.packet(TRANSLATE_PATH, '/var/www/%s/index.html' % request.user)
        elif uri[:18] == '/cached_want_user/':
            response.packet(TRANSLATE_BASE, '/cached_want_user/')
            if want is None or TRANSLATE_USER not in want:
                response.want(TRANSLATE_USER)
                return

            response.packet(TRANSLATE_AUTH, 'dummy')
            if request.user is None:
                response.packet(TRANSLATE_REGEX, r'^@(.*)$')
                response.packet(TRANSLATE_REGEX_ON_USER_URI)
                response.status(403)
            else:
                response.packet(TRANSLATE_REGEX, r'^(.+)@(.*)$')
                response.packet(TRANSLATE_REGEX_TAIL)
                response.packet(TRANSLATE_REGEX_ON_USER_URI)
                response.packet(TRANSLATE_PATH, '/:')
                response.packet(TRANSLATE_EXPAND_PATH, r'/var/www/\1/\2')
        elif uri[:16] == '/file_not_found/':
            if file_not_found is not None:
                assert file_not_found == 'hansi'
                response.packet(TRANSLATE_BASE, '/file_not_found/')
                response.status(204)
                return

            self._handle_local_file('/var/www' + uri[15:], response)
            response.packet(TRANSLATE_FILE_NOT_FOUND, 'hansi')
        elif uri[:17] == '/directory_index/':
            if directory_index is not None:
                assert directory_index == 'abc'
                response.packet(TRANSLATE_BASE, '/directory_index/')
                response.packet(TRANSLATE_REGEX, "^(.*)$")
                response.packet(TRANSLATE_REGEX_TAIL)
                response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'directory_index.py'))
                response.pair('DIRECTORY', 'dummy')
                response.packet(TRANSLATE_EXPAND_PAIR, r'DIRECTORY=/var/www/\1')
                return

            self._handle_local_file('/var/www' + uri[16:], response)
            response.packet(TRANSLATE_DIRECTORY_INDEX, 'abc')
        elif uri[:5] == '/ctl/':
            self._handle_local_file('/var/www' + uri[4:], response)
            response.packet(TRANSLATE_CONTENT_TYPE_LOOKUP, 'xyz')
        elif raw_uri[:9] == '/hosting/':
            self._handle_hosting(request, response, '/hosting/', raw_uri[9:])
        elif raw_uri[:7] == '/probe/':
            self._handle_probe(request, response, '/probe/', raw_uri[7:])
        elif raw_uri[:6] == '/view/':
            response.packet(TRANSLATE_BASE, '/view/')
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_CGI, os.path.join(cgi_path, 'view.py'))
            response.packet(TRANSLATE_PATH_INFO, '/')
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe.sed'))
            response.view('foo')
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe2.sed'))
            response.view('bar')
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe.sed'))
            response.packet(TRANSLATE_FILTER)
            response.pipe(os.path.join(cgi_path, 'pipe2.sed'))
            response.view('raw')
        elif raw_uri[:11] == '/read_file/':
            if request.read_file is None:
                response.packet(TRANSLATE_BASE, '/read_file/')
                response.packet(TRANSLATE_READ_FILE, '/tmp/foo')
            else:
                response.path('/var/www/' + request.read_file)
        elif raw_uri[:16] == '/regex_host_uri/':
            response.packet(TRANSLATE_BASE, '/regex_host_uri/')
            response.packet(TRANSLATE_EASY_BASE)
            response.packet(TRANSLATE_REGEX, "^(.*)$")
            response.packet(TRANSLATE_REGEX_TAIL)
            response.packet(TRANSLATE_REGEX_ON_HOST_URI)
            response.path('/var/www/')
            response.packet(TRANSLATE_EXPAND_PATH, r'/var/www/\1')
        elif raw_uri[:14] == '/session_site/':
            response.packet(TRANSLATE_SESSION_SITE, raw_uri[14:])
            response.path('/var/www/')
        elif uri == '/external_session_manager/':
            response.path('/var/www/index.html')
            response.external_session_manager('http://cfatest01.intern.cm-ag/session')
            response.external_session_keepalive(5)
        elif uri[:19] == '/internal_redirect/':
            response.packet(TRANSLATE_INTERNAL_REDIRECT, 'hans')
            response.packet(TRANSLATE_URI, '/proxy/' + uri[19:])
        elif uri[:9] == '/message/':
            response.packet(TRANSLATE_MESSAGE, uri[9:])
        else:
            self._handle_local_file('/var/www' + uri, response,
                                    error_document=True)

        #response.packet(TRANSLATE_FILTER)
        # .... PROXY 'http://cfatest01.intern.cm-ag/filter.py'

    def _handle_request(self, request):
        if request.content_type_lookup is not None:
            return self._handle_content_type_lookup(request.content_type_lookup,
                                                    request.suffix)

        if request.widget_type is not None:
            return self._handle_widget_lookup(request.widget_type)

        if request.login:
            return self._handle_login(request.user, request.password,
                                      request.service, request.listener_tag)

        if request.cron:
            return self._handle_cron(request.cron, request.listener_tag, request.user, request.uri, request.param)

        if request.auth is not None:
            return self._handle_auth(request.auth, request.uri, request.session, request.alt_host)

        if request.pool is not None:
            return self._handle_pool(request.pool, request.listener_tag, request.host)

        if request.error_document:
            log.msg("error %s %s %u" % (request.uri, repr(request.error_document_payload), request.status))
            return Response(protocol_version=2).path('/var/www/%u.html' % request.status).content_type('text/html')

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

        response = Response(protocol_version=2)
        if user is not None:
            response.packet(TRANSLATE_USER, user)
        if session is not None:
            response.packet(TRANSLATE_SESSION, session)

        if request.host is not None and 'untrusted' in request.host:
            response.packet(TRANSLATE_UNTRUSTED, request.host)
        response.vary(TRANSLATE_HOST, TRANSLATE_PARAM)

        if request.uri is not None:
            self._handle_http(request,
                              request.raw_uri, request.uri, request.authorization,
                              request.check, request.want_full_uri, request.want,
                              request.file_not_found, request.directory_index,
                              request.ua_class, response)

        return response

    def _success(self, result):
        self.transport.write(result.finish())
        self._request = None

    def _fail(self, fail):
        log.err(fail)
        self.transport.write(Response(protocol_version=2).status(500).finish())
        self._request = None

    def _handle_packet(self, packet):
        if packet.command == TRANSLATE_BEGIN:
            self._request = Request()
            self._request.packetReceived(packet)
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
        helpers_path = os.path.join(os.getcwd(), 'output/debug')
        cgi_path = os.path.join(os.getcwd(), 'demo/cgi-bin')
        demo_path = os.path.join(os.getcwd(), 'demo', 'htdocs')

        src_dir = os.path.join(os.getenv('HOME'), 'git')
        if not os.path.isdir(os.path.join(src_dir, 'cgi-coma')):
            if os.path.isdir('../cgi-coma'):
                src_dir = os.path.join(os.getcwd(), '..')
            elif os.path.isdir('../../cgi-coma'):
                src_dir = os.path.join(os.getcwd(), '../..')

        was_examples_path = os.path.join(src_dir, 'libwas')
        coma_fastcgi = os.path.join(src_dir, 'cgi-coma/build/cm4all-coma-fastcgi')
        coma_was = os.path.join(src_dir, 'cgi-coma/build/coma-was')
        coma_demo = os.path.join(src_dir, 'cgi-coma/demo')
        image_processor_path = os.path.join(src_dir, 'image-processor/src')
        ticket_fastcgi_dir = os.path.join(src_dir, 'mod_ticket/src')
        xslt_fastcgi = os.path.join(src_dir, 'filters/build/xslt')
        xmlstrip = os.path.join(src_dir, 'filters/build/xmlstrip')
        sed_fastcgi = os.path.join(src_dir, 'sed/sed/fsed')
        davos_plain = os.path.join(src_dir, 'davos/build/davos-plain')
        davos_od = os.path.join(src_dir, 'davos/build/davos-od')
        od_conf = '/home/max/people/cmag/od/od.conf'
        od_section = 'test'

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
