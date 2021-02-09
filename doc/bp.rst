beng-proxy
##########


Features
========

:program:`beng-proxy` delivers resources via HTTP. In the most simple form, it it
provides a resource in pass-through mode, acting as an HTTP proxy.

It caches resources if possible.

It can filter any resources by POSTing it to a HTTP server, e.g. to
apply XSLT to a XML resource.

On HTML resources, it can apply a simple template language. This
language provides commands to insert another HTML page, which is called
**Widget**.

Widgets
-------

A **Widget** is an object which can be inserted into a web site. It is
rendered by a Widget server into HTML.

We do not assume that we can trust the widget server. As a consequence,
we have to ensure that a malicious widget server cannot compromise the
security of :program:`beng-proxy`, the client or even other widget servers.

There is a global registry for well-known preconfigured widgets. The
user can also choose to run his own (non-registered) widget server. In
fact, any public HTTP server should be able to act as a widget server.

Cookies
-------

:program:`beng-proxy` can be accessed with cookies switched off. It includes a
full-featured session management and provides cookies for the widget
servers.

:program:`beng-proxy` maintains the client’s session id in either a cookie or as
part of the URI. In its local session storage, it holds all cookies
which were created by the widget servers. This way, the client gets to
see only the one session id, disregarding how much session information
may be managed by :program:`beng-proxy`.

JavaScript
----------

Since all widgets are put together into a single HTML page, all of the
JavaScript runs in the same security context. That will open the door
for malicious widget servers, which are now able to take over the full
web site, including all other widgets. For that reason, only
well-known and trusted widget servers should be allowed to be
inlined. All other widget must be embedded in an ``IFRAME`` in another
domain.

Forms
-----

:program:`beng-proxy` itself does not use the query string and the
request body.  Both is forwarded to the “focused” widget. See
:ref:`focus` for information on widget focus.

Installation
============

:program:`beng-proxy` requires a Debian Jessie operating system: Linux kernel
3.16 and glibc 2.19. For compiling the source code, you need a C++11
compiler, e.g. gcc 4.9.

Install the package ``cm4all-beng-proxy`` and the translation server of
your choice.

Configuration
=============

The file :file:`/etc/cm4all/beng/lb.conf` contains
:program:`beng-proxy`\ ’s configuration. The following options are
available:

``@include``
------------

Include another file. Example::

   @include "foo/bar.conf"
   @include_optional "foo/may-not-exist.conf"
   @include "wildcard/*.conf"

The second line silently ignores non-existing files.

The third line includes all files in the directory ``wildcard`` ending
with ``.conf``.

The specified file name may be relative to the including file.

Variables (``@set``)
--------------------

Set a variable. Within double-quoted strings, variables can be expanded
with ``${name}``. Example::

   @set foo = "192.168.1.42"
   @set bar = "${foo}:80"
   listener {
     bind "${bar}"
   }

At the time of this writing, the concept of variables is not
well-implemented. For example, (backslash) escape sequences don’t work,
and the scope of variables is not defined. For now, use variables only
for very simple things.

.. _translation_servers:

Translation Servers
-------------------

The setting ``translation_socket`` specifies the translation server's
socket.  It can be specified multiple times to support
:ref:`translation deferral <tdefer>`.  Example::

  translation_socket "@translation1"
  translation_socket "@translation2"

The default is ``@translation``.

``listener``
------------

Listen for HTTP requests on the configured address. Example::

   listener {
      bind "*:80"
      tag "foo"
      zeroconf_service "beng-proxy"
   }

This binds to all interfaces on port 80. The (optional) tag is set to
“foo”.

Known attributes:

- ``bind``: an address to bind to. May be the wildcard ``*`` or an
  IPv4/IPv6 address followed by a port. If you omit the port number,
  it will default to 80. Specifying port 0 will auto-select a free
  port (which makes sense only if you publish the listener with
  Zeroconf).  IPv6 addresses should be enclosed in square brackets to
  disambiguate the port separator. Local sockets start with a slash
  :file:`/`, and abstract sockets start with the symbol ``@``.

- ``interface``: limit this listener to the given network interface.

- ``ack_timeout``: close the connection if transmitted data remains
  unacknowledged by the client for this number of seconds. By default,
  dead connections can remain open for up to 20 minutes.

- ``keepalive``: ``yes`` enables the socket option ``SO_KEEPALIVE``.
  This causes some traffic for the keepalive probes, but allows
  detecting disappeared clients even when there is no traffic.

- ``v6only``: ``no`` disables IPv4 support on IPv6 listeners
  (``IPV6_V6ONLY``).  The default is ``yes``.

- ``reuse_port``: ``yes`` enables the socket option ``SO_REUSEPORT``,
  which allows multiple sockets to bind to the same port.

- ``free_bind``: ``yes`` enables the socket option ``IP_FREEBIND``,
  which allows binding to an address which does not yet exist. This is
  useful when the daemon shall be started before all network
  interfaces are up and configured.

- ``tag``: a tag, to be passed to the translation server in a
  :ref:`LISTENER_TAG <t-listener_tag>` packet.

- ``auth_alt_host``: ``yes`` forwards the value of the
  ``X-CM4all-AltHost`` request header to the translation server in
  ``AUTH`` requests.

- ``ssl``: ``yes`` enables SSL/TLS.

- ``ssl_cert``: add a certificate/key pair to the listener. If ``ssl``
  is enabled, at least one pair must be configured; if there is more
  than one, the server will choose one according to the SNI parameter
  received from the client.

- ``ssl_verify`` and ``ssl_ca_cert`` can be used to enable client
  certificate verification (see :ref:`ssl_verify` for details).  To
  generate the request headers ``X-CM4all-BENG-Peer-Subject`` and
  ``X-CM4all-BENG-Peer-Issuer-Subject``, the ``SSL`` request header
  group must be set to ``MANGLE`` (see :ref:`tfwdheader`).

- ``zeroconf_service``: if specified, then register this listener as
  Zeroconf service in the local Avahi daemon. This can be used by
  :program:`beng-lb` to discover pool members.

- ``translation_socket``: if at least one is specified, then this
  translation server is used instead of one from the global
  configuration (see :ref:`translation_servers`).

``ssl_client``
--------------

Configures the SSL/TLS client (for HTTPS). Example::

   ssl_client {
     cert "/etc/ssl/certs/ssl-cert-snakeoil.pem" "/etc/ssl/private/ssl-cert-snakeoil.key"
   }

The section contains a ``cert`` line for each client certificate to be
used for outgoing SSL/TLS connections. Each time a server asks for a
client certificate, :program:`beng-proxy` will look for a matching
certificate for the requested certificate authority.

Instead of letting :program:`beng-proxy` choose a matching
certificate, the translation server can specify a certificate by its
name. To give a certificate a name, add a third parameter::

   ssl_client {
     cert "/etc/ssl/certs/ssl-cert-snakeoil.pem" "/etc/ssl/private/ssl-cert-snakeoil.key" "thename"
   }

.. _certificate:

Now the translation server can send the ``CERTIFICATE`` packet with
payload ``thename`` to select this certificate.

``control``
-----------

See :ref:`config.control`.

.. _config.spawn:

``spawn``
---------

Configures the process spawner. Example::

   spawn {
     default_user "www-data"
     allow_user "www-data"
     allow_group "www-data"
     CPUWeight "50"
     TasksMax "100"
     MemoryMax "16 GB"
     IOWeight "50"
   }

- ``default_user``: a user name which is used if the translation
  server does not specify a user id.

- ``allow_user``: allow child processes to impersonate the given
  user.  This can be a user name (from :file:`/etc/passwd`), a
  numeric user id or an open range (e.g. `2147483648-` which allows
  all user ids from 2147483648 on).

- ``allow_group``: allow child processes to impersonate the given
  group.

- ``CPUWeight``: CPU weight for all spawned processes combined
  (:math:`1..10000`). ``systemd``\ ’s default is 100.

- ``TasksMax``: maximum number of tasks (:math:`1..`). ``systemd``
  sets no limit by default.

- ``MemoryMax``: absolute limit on the combined memory usage of all
  spawned processes. Value is in bytes and may be postfixed with
  ``kB``, ``MB``, ``GB`` or ``TB``. ``systemd`` sets no limit by
  default.

- ``IOWeight``: IO weight for all spawned processes combined
  (:math:`1..10000`). ``systemd``\ ’s default is 100.

``set``
-------

Tweak global settings. Most of these are legacy from the old ``–set``
command-line option. Do not confuse with ``@set``, which sets
configuration parser variables! Syntax::

   set NAME = "VALUE"

The following settings are available:

- ``session_cookie``: The name of the session cookie. The default
  value is ``beng_proxy_session``.

- ``session_cookie_same_site``: Enable the ``SameSite`` attribute in
  the session cookie (see `RFC 6265 5.3.7
  <https://tools.ietf.org/html/draft-ietf-httpbis-rfc6265bis-02#section-5.3.7>`__).
  Supported values are ``strict`` and ``lax``.

- ``dynamic_session_cookie``: Append a suffix to the session cookie
  generated from the ``Host`` request header if set to ``yes``. This
  is a measure to increase sessions separation of different hosts
  under the same domain, accounting for mainstream user agents that
  are known to ignore the ``Domain`` cookie attribute. It is not
  guaranteed to be collision-free.

- ``session_idle_timeout``: After this duration, a session expires,
  unless it gets refreshed by a request.  Example: :samp:`30 minutes`.

- ``max_connections``: The maximum number of incoming HTTP connections.

- ``tcp_stock_limit``: The maximum number of outgoing TCP connections
  per remote host. 0 means unlimited, which has shown to be a bad
  choice, because many servers do not scale well.

- ``fastcgi_stock_limit``: The maximum number of child processes for
  one FastCGI application. 0 means unlimited.

- ``fastcgi_stock_max_idle``: The maximum number of idle child
  processes for one FastCGI application. If there are more than that, a
  timer will incrementally kill excess processes.

- ``was_stock_limit``: The maximum number of child processes for one
  WAS application. 0 means unlimited.

- ``was_stock_max_idle``: The maximum number of idle child processes
  for one WAS application. If there are more than that, a timer will
  incrementally kill excess processes.

- ``http_cache_size``: The maximum amount of memory used by the HTTP
  cache. Set to 0 to disable the HTTP cache.

- ``http_cache_obey_no_cache``: Set to ``no`` to ignore ``no-cache``
  specifications in ``Pragma`` and ``Cache-Control`` request headers.

- ``filter_cache_size``: The maximum amount of memory used by the
  filter cache. Set to 0 to disable the filter cache.

- ``translate_cache_size``: The maximum number of cached translation
  server responses. Set to 0 to disable the translate cache.

- ``translate_stock_limit``: The maximum number of concurrent
  connections to the translation server. Set to 0 to disable the limit.
  The default is 64.

- ``verbose_response``: Set to ``yes`` to reveal internal error
  messages in HTTP responses.

- ``session_save_path``: A file path where all sessions will be saved
  periodically and on shutdown. On startup, it will attempt to load the
  sessions from there. This option allows restarting the server without
  losing sessions.

All memory sizes can be suffixed using ``kB``, ``MB`` or ``GB``.

Cluster Options
---------------

To run :program:`beng-proxy` as a :program:`beng-lb` cluster node with sticky sessions,
each node needs special configuration. It needs to generate new session
numbers in a way that allows :program:`beng-lb` to derive the cluster node from
it.

To do that, specify the two command line options ``--cluster-size``
and ``--cluster-node`` to each :program:`beng-proxy` node. Example for
a cluster with 3 nodes::

   first# cm4all-beng-proxy --cluster-size=3 --cluster-node=0 ...
   second# cm4all-beng-proxy --cluster-size=3 --cluster-node=1 ...
   third# cm4all-beng-proxy --cluster-size=3 --cluster-node=2 ...

Each node number is assigned to exactly one cluster node.

The according ``lb.conf`` would look like this::

   pool foo {
     sticky "session_modulo"
     member first:http
     member second:http
     member third:http
   }

The ordering of nodes matters. :program:`beng-lb` assumes that the
first node runs with ``--cluster-node=0``, the second node runs with
``--cluster-node=1`` and so on.

Running
=======

Signals
-------

``SIGTERM`` on the master process initiates shutdown.

On ``SIGHUP``, the error log file is reopened, all caches are flushed
and all spawned child processes are faded out (see
:ref:`FADE_CHILDREN <fade_children>`).

Triggers
--------

The Debian trigger ``cm4all-apps-changed`` reloads all spawned
applications. It shall be invoked after updating application packages
(or widgets).

Tuning
======

Optimized Build
---------------

The default package ``cm4all-beng-proxy`` is built with debugging code
enabled. It is about 2-10 times slower than the optimized build. If
performance really counts, you should install the package
``cm4all-beng-proxy-optimized`` instead (and restart the daemon).

To switch back to the debug build, uninstall
``cm4all-beng-proxy-optimized`` and then reinstall ``cm4all-beng-proxy``
to get the old :file:`/usr/sbin/cm4all-beng-proxy` back. Finally, restart
the daemon.

Resource Limits
---------------

:program:`beng-proxy` needs to open a lot of file handles at a time, because it
serves many connections in one process. Make sure that the file handle
limit is adequate. The default init script sets it to 65536. The only
reason set that limit at all is to detect bugs (file descriptor leaks).

Keep in mind that :program:`beng-proxy` may open more than one file descriptor
per connection. For example, a connection to a WAS application needs 3
file descriptors.

Connection Limits
-----------------

:program:`beng-proxy` is very good at managing lots of incoming connections, and
manages system resources economically. The default value is 8192.

There are good reasons to limit the number of outgoing connections per
host (``tcp_stock_limit``): most servers don’t handle so many
connections as well as :program:`beng-proxy`, and performance degrades when there
are too many. By default, there is no limit.

Pipe Limits
-----------

Linux has a global setting called
:file:`/proc/sys/fs/pipe-user-pages-soft` which controls how many
pages of memory one user may allocate for pipe buffers.  The default
setting ``16384`` is too small for :program:`beng-proxy`, and pipes
will max out at one page, which decreases performance.  It is
recommended to increase it to ``1048576`` by adding to
:file:`/etc/sysctl.d`::

    fs.pipe-user-pages-soft = 1048576


Firewall
--------

Benchmarks have demonstrated that Netfilter (and its connection
tracking) account for a good amount of the CPU load on a busy server. A
good server does not need to depend on a firewall for security: rather
than blocking protocols and ports, the administrator should make sure
that these services aren’t bound to public interfaces in the first
place. An internal services bound on all interfaces is an indicator for
misconfiguration.

It is a good idea to disable the firewall (in the kernel configuration)
and audit all listeners. If you cannot do without a firewall, you can
disable connection tracking for :program:`beng-proxy` connections::

   table raw {
     chain PREROUTING proto tcp dport http NOTRACK;
     chain OUTPUT proto tcp sport http NOTRACK;
   }

Cacheable Widgets and Containers
--------------------------------

If you do a lot of direct communication with widgets, its container
should be cacheable. If not, the container will be queried each time a
request for a widget is handled. On pages with many widgets, you
should try to make all of them cacheable. See :ref:`caching` for
details.

Disabling Widget Options
------------------------

Don’t enable widget options when you don’t need them. That affects the
options “processor”, “container”, “stateful” and others. Each of them
adds some bloat to the response handler, and slows down the
application.  See :ref:`registry` for details.

Load Balancing
--------------

If a machine serving a resource is too slow, you may be able to
parallelize its work. Note that this increases throughput, but usually
does not reduce latency considerably. See :ref:`balancing`.

.. _stopwatch:

The Stopwatch
-------------

The stopwatch measures the latency of external resources (e.g. remote
HTTP servers, CGI and pipe programs). It is only available in the
debug build (compile-time option ``--enable-stopwatch``).

Example output::

   stopwatch[172.30.0.23:80 /test.py]: request=5ms headers=85ms end=88ms (beng-proxy=1+2ms)

Here, the HTTP request to ``172.30.0.23:80`` was sent within 5
milliseconds. After 85 milliseconds, the response headers were
received, and after 3 more milliseconds, the response body was
received. All of these refer to wallclock time, relative to the start
of the operation.  Each client library may have its own set of
breakpoints.

During this HTTP request, :program:`beng-proxy` consumed 3
milliseconds of raw CPU time (not wallclock time): 1 millisecond in
user space, and 2 milliseconds for the kernel.

Resources
=========

:program:`beng-proxy` delivers resources to its HTTP clients.  It
obtains these resources from several sources.

.. _static:

Static files
------------

Local “regular” files can be served by :program:`beng-proxy`. This is
the fastest mode, and should be preferred, if possible. The ``Range``
request header is supported (bytes only).

.. _xattr:

Content type
~~~~~~~~~~~~

In contrast to most other web servers, :program:`beng-proxy` does not
use the file name to determine the ``Content-Type`` response
header. Instead, it reads this information from *extended
attributes*. The programs ``getfattr`` and ``setfattr`` (Debian
package ``attr``) enable you to read and write attributes::

   setfattr -n user.Content-Type -v "text/html; charset=utf8" \
     /var/www/index.html

Some file systems need explicit support for extended attributes (mount
option ``user_xattr``). On XFS, extended attributes are always enabled.

ETag
~~~~

The ``ETag`` response header is read from the ``user.ETag`` extended
attribute (see :ref:`xattr`). If none is present, it is generated from
the inode number and the modification time. The request headers
``If-Match`` and ``If-None-Match`` are supported.

Expires
~~~~~~~

If the ``user.MaxAge`` attribute exists, it is parsed as a decimal
integer. The ``Expires`` response header is then generated by adding
this number of seconds to the current time stamp. The maximum accepted
value for ``user.MaxAge`` is one year.

Directory index
~~~~~~~~~~~~~~~

For security (by obscurity) reasons, :program:`beng-proxy` has no code for
generating directory listings.

.. _delegate:

Delegates
---------

A “delegate” is a helper program which opens a local file and passes
the file descriptor to :program:`beng-proxy`. The major reason for
using a delegate is to take advantage of the kernel’s validation: the
delegate program may run with different privileges, different resource
limits or in a chroot/vserver.

The delegate reads requests on standard input. The protocol is similar
to the translation protocol. The file descriptor is sent to
:program:`beng-proxy` in a ``SOL_SOCKET`` ancillary message.

If the ``DELEGATE`` translation packet was followed by a
``DOCUMENT_ROOT`` packet, then all helper processes are grouped by
their document root, and the ``DOCUMENT_ROOT`` environment variable is
set.

.. _http:

HTTP proxying
-------------

:program:`beng-proxy` implements an HTTP client, which allows it to
act as a reverse HTTP proxy server. You should never make
:program:`beng-proxy` connect to itself.

.. _caching:

Caching
~~~~~~~

Responses from the remote servers are cached, if possible. To allow
proper caching, the remote server must set the response headers
``Last-Modified``, ``Expires`` and ``ETag`` properly. Additionally,
they should understand the according request headers
``If-Modified-Since`` and ``If-Unmodified-Since``, ``If-Match``,
``If-None-Match``.

The cache is local to a :program:`beng-proxy` worker.

Connection pooling
~~~~~~~~~~~~~~~~~~

:program:`beng-proxy` attempts to use HTTP 1.1 keep-alive, to be able to reuse
existing connections to a remote server.

.. _balancing:

Load balancing, failover
~~~~~~~~~~~~~~~~~~~~~~~~

For a remote URL, more than one server may be specified. ``beng-proxy``
tries to use all of these equally. If one server fails on the socket
level, ``beng-proxy`` ignores it for a short amount of time.

Forwarded headers
~~~~~~~~~~~~~~~~~

Not all request and response headers are forwarded, for various reasons:

- hop-by-hop headers (`RFC 2616 13.5.1
  <https://www.ietf.org/rfc/rfc2616.html#section-13.5.1>`__) must not
  be forwarded

- headers describing the body are not forwarded if there is no body

- some headers reveal otherwise private information about the
  communication partner at the other end (e.g. IP address)

- some servers rely on the authenticity of the ``X-CM4all-BENG-User``
  header

- due to imponderable security implications, much of the header
  forwarding is opt-in

By default, only the following original request headers are forwarded to
the remote HTTP server:

- the ``Accept-*`` headers

- ``User-Agent``

- ``Cache-Control``

- in the presence of a forwarded request body: ``Content-Type`` and the
  other ``Content-*`` headers

- ``Cookie2`` is taken from the current session

Response headers forwarded to ``beng-proxy``\ ’s client:

- ``Age``, ``ETag``, ``Cache-Control``, ``Last-Modified``,
  ``Retry-After``, ``Vary``, ``Location``

- ``Content-Type`` and the other ``Content-*`` headers

- ``Set-Cookie2`` is generated from the current session

The translation server can change the header forwarding policy, see
:ref:`tfwdheader`.

SSL/TLS
~~~~~~~

To enable SSL/TLS, specify a ``https://`` URL in the ``HTTP`` packet.

After that, the :ref:`CERTIFICATE <certificate>` packet can choose a
client certificate.

.. _cgi:

CGI and FastCGI
---------------

Local CGI programs may be used to generate dynamic resources.

CGI/FastCGI resources are cached in the same manner as remote HTTP
resources.

.. _was:

WAS
---

Web Application Socket (WAS) is a protocol that can let a child
process render a resource, similar to FastCGI. Unlike FastCGI, it
copies raw data through separate pipes, which allows using the
``splice()`` system call for efficient zero-copy transfer.

.. _pipe:

Pipe filters
------------

A pipe is a program which filters a resource by reading it from standard
input, and writing the result to standard output. This option cannot be
used to generate a resource, but only for resource filters. The same can
be achieved with CGI, but pipes are simpler to implement, because they
do not need to bother with HTTP status code and headers.

.. _nfs:

NFS
---

:program:`beng-proxy` can serve files right from a NFSv3 server without having to
mount it locally. The NFS server must accept “insecure” connections,
that is connections from non-privileged source ports. Don’t fear,
calling it “insecure” is an exaggeration; that option’s name was chosen
long ago, when people thought the concept of “privileged ports” would
benefit security.

Three translation response packets are necessary to construct an NFS
resource address; example::

   NFS_SERVER "1.2.3.4"
   NFS_EXPORT "/srv/nfs/foo"
   PATH "/index.html"

This mounts the path :file:`/srv/nfs/foo` from server ``1.2.3.4`` and
serves the file ``index.html``. The leading slashes are necessary.

The options above are compatible with ``BASE`` and ``EXPAND_PATH``.

.. _lhttp:

Local HTTP
----------

“Local HTTP” is a way for :program:`beng-proxy` to launch local HTTP servers. An
address for a “local HTTP” resource contains at least:

- a server program

- a request URI

Optional attributes:

- command-line arguments (one or more ``APPEND`` packets)

- a “Host” request header (packet ``LHTTP_HOST``)

- concurrency (packet ``CONCURRENCY``)

How it works: :program:`beng-proxy` spawns the specified process with
a bound listener socket on file descriptor 0. The server program then
accepts regular HTTP connections on this listener socket.


Remote Control Protocol
=======================

See :ref:`control`.


Logging Protocol
================

See :ref:`log`.


Widget protocol
===============

A widget server is simply an HTTP server. Its content type must be
``text/html`` or ``text/xml``.

Hyperlinks
----------

A widget may provide hyperlinks, e.g. with anchor elements or with FORM
elements.

“Internal links” are links which are relative to the widget’s base URI -
these links can be loaded into the widget’s dock. In CGI, this feature
is called “PATH_INFO”. An internal link may include a query string.

“External URIs” are not relative, they should load in a new browser
window.

Redirection
-----------

Widgets can send the usual HTTP redirection responses (status ``3xx``).
The new location must be below the widget’s base URI.

:program:`beng-proxy` is currently limited to sending a ``GET`` request following
the redirect, because it does not save the request body. This is always
correct for “303 See Other”, but may not be for the other redirection
types. Widget servers should therefore always redirect with “303 See
Other” as follow-up to a POST request.

.. _focus:

Focus
-----

To navigate inside a widget, the widget must be “focused”. A focus can
be assigned by clicking on a hyperlink that was generated using the
“focus” URI rewriting mode (see :ref:`c:mode <c_mode>`).

A link pointing to the focused widget may change its current URI
(relative to the widget’s base URI). If the HTTP request contains a
query string or a request body, they are forwarded to that widget,
instead of being sent to the template.

POSTing and other methods
-------------------------

Making the browser send a request body with a POST request is possible.
It is recommended that you send a “303 See Other” redirect as a response
to a POST request. Always reckon that :program:`beng-proxy` may request a
resource multiple times, even without interaction of the browser.

The same is true for other HTTP methods: ``PUT``, ``DELETE`` and others
are passed to the focused widget (see :ref:`focus`).

Session tracking
----------------

A widget may use HTTP cookies for session tracking, even if the browser
does not support it - :program:`beng-proxy` will take care of it. The widget
should not include some kind of session identification in the URI.

These cookies are not available in JavaScript. Besides that, it would be
a bad practice to use cookies in JavaScript which are not actually
evaluated by the server (and cannot be used by the widget server in this
case, since :program:`beng-proxy` does not forward them). These cookies would
generate a lot of network load for no good, which would have to go
through the visitor’s narrow upstream with every request.

It is recommended to use (cookie based) sessions only if really
required. In many situations, there are more elegant solutions, like
storing the current state of a widget in its current URI (path info).

.. _authentication:

Authentication
--------------

.. _http_auth:

HTTP-level Authentication
~~~~~~~~~~~~~~~~~~~~~~~~~

A translation response containing ``HTTP_AUTH`` enables HTTP-based
authentication according to `RFC 2617
<https://www.ietf.org/rfc/rfc2617.html>`__.  The packet may contain an
opaque payload.  Additionally, the translation server should send
``WWW_AUTHENTICATE`` and ``AUTHENTICATION_INFO``, which will be sent
to the client in the ``WWW-Authenticate`` and ``Authentication-Info``
response headers.

Without an ``Authorization`` request header, the HTTP request will
result in a ``401 Unauthorized`` response (with headers
``WWW-Authenticate`` and ``Authentication-Info``).

If the ``Authorization`` header is available, :program:`beng-proxy`
submits it to the translation server in an ``AUTHORIZATION`` packet
and echoes the ``HTTP_AUTH`` packet.  The translation server
responds with one of:

- ``USER`` specifying the user handle to be forwarded in
  ``X-CM4all-BENG-User`` request headers (optionally followed by
  ``MAX_AGE``, because :program:`beng-proxy` is allowed to cache these
  responses)

- ``STATUS=401`` if the ``Authorization`` value was rejected

Example conversation:

#. :program:`beng-proxy`: ``URI=/protected/foo.html``

#. translation server:
   ``PATH=/var/www/protected/foo.html HTTP_AUTH=opaque
   WWW_AUTHENTICATE='Basic realm="Foo"'``

#. :program:`beng-proxy`: ``HTTP_AUTH=opaque AUTHORIZATION='Basic
   QWxhZGRpbjpvcGVuIHNlc2FtZQ=='``

#. translation server:
   ``USER=Aladdin MAX_AGE=300``

HTTP-level Authentication (old)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:program:`beng-proxy` supports HTTP-level authentication according to
`RFC 2617 <https://www.ietf.org/rfc/rfc2617.html>`__.
It forwards the ``Authorization`` request header to the translation
server wrapped in a ``AUTHORIZATION`` packet, and allows the translation
server to send ``WWW-Authenticate`` and ``Authentication-Info`` response
headers back to the client, wrapped in ``WWW_AUTHENTICATE`` and
``AUTHENTICATION_INFO``.

Token Authentication
~~~~~~~~~~~~~~~~~~~~

A translation response containing ``TOKEN_AUTH`` enables token-based
authentication.  The packet may contain an opaque payload.

The token is extracted from the ``access_token`` query string parameter.
To check it, :program:`beng-proxy` sends a new request with the
following packets:

- ``TOKEN_AUTH`` (echoing the response packet)
- ``AUTH_TOKEN`` contains the ``access_token`` query string parameter
  (unescaped)
- ``URI`` is the full request URI with only the ``auth_token`` query
  string parameter removed
- ``HOST``

If no ``access_token`` parameter was present, :program:`beng-proxy`
checks if a ``USER`` is already set in the current session; if yes,
then translation request will be skipped completely.  If not, then the
``TOKEN_AUTH`` request will be sent, but without an ``AUTH_TOKEN``
packet.

The translation server may now reply:

- ``STATUS`` (optionally with ``MESSAGE``) on error
- ``REDIRECT`` (optionally with ``STATUS``), e.g. to redirect to a
  login page
- ``DISCARD_SESSION``, ``SESSION``, ``USER``: the session is updated
  and the client will be redirected to the current URI, but without
  the ``auth_token`` query string parameter


Application level Authentication
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Authentication is supported in the translation protocol. After the
translation server sets the ``USER`` session variable to a non-empty
string, the session is presumed to be authenticated. This user variable
is passed to widget servers in the proprietary ``X-CM4all-BENG-User``
request header. The user is logged out when the translation sends an
empty ``USER`` packet.

The ``CHECK`` packet
~~~~~~~~~~~~~~~~~~~~

On a protected resource, the translation server may send the ``CHECK``
packet together with the normal response. Now ``beng-proxy`` queries the
translation server again, sending the same request and a copy of the
``CHECK`` packet. The translation server may now verify the current
session, redirect to a login page, or anything else needed to
authenticate the user. The response to this second translation request
may be a resource address as usual, or the ``PREVIOUS`` packet, which
indicates that the first translation shall be used.

While the first response is usually cached for a long time, the second
one may specify a short ``MAX_AGE`` value. This means the latter is sent
more often, but since it refers to the former, it is very small.

Example 1, unauthenticated user logs in:

#. :program:`beng-proxy`: ``URI=/protected/foo.html``

#. translation server:
   ``PATH=/var/www/protected/foo.html SESSION=1234 CHECK=xyz``

#. :program:`beng-proxy`: ``URI=/protected/foo.html SESSION=1234 CHECK=xyz``

#. translation server:
   ``MAX_AGE=0 STATUS=403 CGI=/usr/lib/cgi-bin/login.pl``

#. user enters his credentials, login.pl marks the session
   “authenticated”, redirects back to the original URI

#. :program:`beng-proxy`: ``URI=/protected/foo.html SESSION=1234 CHECK=xyz``
   (from the cached translation response)

#. translation server: ``MAX_AGE=300 VARY=SESSION PREVIOUS``

Example 2, authenticated user:

#. :program:`beng-proxy`: ``URI=/protected/foo.html SESSION=2345``

#. translation server: ``PATH=/var/www/protected/foo.html CHECK=xyz``

#. :program:`beng-proxy`: ``URI=/protected/foo.html SESSION=2345 CHECK=xyz``

#. translation server: ``MAX_AGE=300 VARY=SESSION PREVIOUS``

.. _auth:

The ``AUTH`` packet
~~~~~~~~~~~~~~~~~~~

``AUTH`` provides another authentication protocol that was designed to
support SAM and similar authentication services. If the client is not
already authenticated, the translation server receives a dedicated
authentication request, echoing the ``AUTH`` packet. Additionally, it
receives the full request URI in the ``URI`` packet, the “Host” header
in the ``HOST`` packet and the session id in the ``SESSION`` packet.

The response to this ``AUTH`` request may be one of the following:

- ``USER`` specifying the new session user (optionally followed by
  ``MAX_AGE``)

- ``REDIRECT`` (optionally with ``STATUS``)

- ``BOUNCE`` (optionally with ``STATUS``)

- ``STATUS``

Only clients with a fresh ``USER`` will be allowed to actually perform
the request.

Caching ``AUTH`` requests is not implemented properly; to be
future-proof, the response **must** begin with ``MAX_AGE=0``.
Compatibility will not be guaranteed without it.

Example:

#. ...

#. translation server: ... ``SESSION=opaque1``

#. :program:`beng-proxy`: ``URI=/foo.html HOST=example.com``

#. translation server: ... ``AUTH=opaque2``

#. :program:`beng-proxy`:
   ``AUTH=opaque2 SESSION=opaque1 URI=/foo.html;a=b?c=d HOST=example.com``

#. translation server: ``MAX_AGE=0 USER=hans MAX_AGE=300``

Note the two ``MAX_AGE`` packets. The first one disables caching for the
whole translation response (mandatory, see above) and the second one
enforces revalidation every 5 minutes.

An alternative to ``AUTH`` is the packet ``AUTH_FILE`` which specifies
the path to a file containing the ``AUTH`` payload (no more than 64
bytes). This path can be specified dynamically using
``EXPAND_AUTH_FILE``.

Additionally, ``APPEND_AUTH`` may specify a payload that will be
appended to the contents of the ``AUTH_FILE``. There’s also
``EXPAND_APPEND_AUTH``.

If the listener option ``auth_alt_host`` is enabled, then the request
header ``X-CM4all-AltHost`` will be forwarded to the translation server
in a ``ALT_HOST`` translation packet.

Referrer
--------

The ``Referer`` request header is not supported.

Views
-----

A widget class may have a number of named views. Only the “default” view
has no name, and it cannot be selected explicitly. A view may have a
different server address, different transformations and other settings.

A view other than the default one can be selected in three different
ways:

- in the template with the element ``c:view``

- as a request argument from the client

- as a HTTP response header from the widget server

For security reasons, the view a client is allowed to choose is limited.
A view that has an address can only be selected by the template, to
avoid unauthorized access to vulnerable areas. If the view chosen by the
template enables the HTML processor with the “container” flag,
:program:`beng-proxy` disallows the client to switch to another view that is not
a “container”, to avoid exposing the template’s widget parameters
(unless the response is not processable). Switching to a view without an
address is always allowed if the previous view does not make the widget
a container.

While the limitations described above do not guarantee real security, it
was decided that it would be an acceptable compromise.

The widget server can select the view with the response header
``X-CM4all-View``. Just the list of transformations (processor, filter)
will be used, the new URI of the view will be ignored. At this point, a
“partial” request for a child widget may be discarded already when the
previous view did not declare the widget as a “container”. Due to these
side effects, this feature should be avoided if possible; it is better
to select the view in the request.

Generic Views
~~~~~~~~~~~~~

Regular HTTP resources can have views, too. Usually, only the default
view is used. There is only one way to select a different view: by using
the ``X-CM4all-View`` response header.

.. _processor:

The Beng Template Language
==========================

The :program:`beng-proxy` template language defines commands which may be
inserted into XHTML stream. They are implemented as XML elements and
attributes with the prefix ``c:``. If you care about validating the
processor input, you must declare the XML namespace ``c:``. There is
currently no suggested namespace URI, and :program:`beng-proxy` does not actually
care, because it does not implement a full-featured XML parser.

Options
-------

The following translation packets may be used to configure the
processor:

- ``PROCESS``: Enables the processor.

- ``CONTAINER``: Allows embedding other widgets.

- ``SELF_CONTAINER``: Allows embedding more instances of the current
  widget type.

- ``GROUP_CONTAINER``: Allow this widget to embed instances of this
  group. This can be specified multiple times to allow more than one
  group. It can be combined with ``SELF_CONTAINER``.

- ``WIDGET_GROUP``: Assign a group name to the widget type. This is
  used by ``GROUP_CONTAINER``.

- ``FOCUS_WIDGET``: Set the default URI rewriting options to
  “base=widget, mode=focus”.

- ``ANCHOR_ABSOLUTE``: A slash at the beginning or a URI refers to the
  widget base, not to the server root.

- ``PREFIX_CSS_CLASS``: CSS class names with leading underscore get a
  widget specific prefix, see :ref:`prefix_css_class`.

- ``PREFIX_XML_ID``: XML ids with leading underscore get a widget
  specific prefix, see :ref:`prefix_css_class`.

- ``PROCESS_STYLE``: Shall the processor invoke the CSS processor for
  “style” element/attribute contents?

Adding a widget
---------------

To add a widget, use the following command::

   <c:widget id="foo" type="date" />

The following attributes may be specified:

- ``id``: unique identification of this widget; this is
  required for proper session and form management if there are several
  widgets with the same server URI
- ``type``: registered name of the widget server

- ``display``: specifies how the widget is to be displayed: ``inline``
  is the default, and inserts the widget’s HTML code into the current
  page; ``none`` does not display the widget, but it may be referenced
  later (see section :ref:`frames`)
- ``session``: the scope of the widget session (which widgets with the
  same id share the same session data?): ``resource`` is the default and
  means that two documents have different sessions; ``site`` means
  documents in the same site share session data

Registered widgets are not yet implemented.

Passing arguments to widgets
----------------------------

Example::

   <c:widget id="foo" type="date">
     <c:parameter name="timezone" value="PST" />
     <c:path-info value="/bla" />
   </c:widget>

``parameter`` elements adds query string parameters. These are added to
the query string provided by the browser. In the value, the standard XML
entities ``amp``, ``quot``, ``apos``, ``lt``, ``gt`` are recognized.

There may be one ``path-info`` element whose value is appended to the
widget URI, if none was sent by the browser.

This is not a reliable way to transfer bulk data. Only very short values
should be passed this way to a widget. There is no guarantee that
:program:`beng-proxy` or other web servers can cope with URIs longer than 2 kB.
If your widget comes even close, you should reconsider your approach.

As usual: never trust user input! The widget server cannot see if input
came from the template or from the user’s browser.

Passing HTTP headers to widgets
-------------------------------

Example::

   <c:widget id="foo" type="date">
     <c:header name="X-CM4all-Foo" value="Bar" />
   </c:widget>

``header`` elements create HTTP request headers. Headers are replaced,
i.e. if a header with such a name was about to be forwarded from the
client to the widget, the client’s value will be removed. In the header
name, only letters, digits and the dash is allowed. It must start with
“X-”.

Selecting the widget view
-------------------------

Example::

   <c:widget id="foo" type="bar">
     <c:view name="raw"/>
   </c:widget>

The ``c:view`` element selects the transformation view for this widget.
It can be one of the view names provided by the widget registry (i.e.
the translation server).

.. _entities:

Variable substitutions
----------------------

:program:`beng-proxy` defines special entities beginning with ``c:`` for its
purposes. Namespaced entities are not actually allowed in XML or HTML,
and this is only an interim solution until the javascript filter is
finished. These entities are (unlike normal HTML entities) also expanded
in ``SCRIPT`` elements.

- ``&c:local;``: the “local” URI of this widget class (see
  :ref:`LOCAL_URI <local_uri>`).

- ``&c:type;``: the class name of this widget

- ``&c:class;``: the quoted class name of this widget

- ``&c:id;``: the id of this widget
- ``&c:path;``: the location of this widget
- ``&c:prefix;``: XML id and Javascript prefix
- ``&c:uri;``: absolute external URI of the current page; use this
  variable for redirecting

- ``&c:base;``: base URI of the current page (i.e. without :program:`beng-proxy`
  arguments and without the query string)
- ``&c:frame;``: the top widget in this frame (if any)

- ``&c:view;``: the name of the current view

Before inserting, the values are escaped using the standard XML
entities.

.. _rewrite:

Relative URIs
-------------

Relative links are difficult with :program:`beng-proxy`, because the browser
interprets links as relative to the document by default. A widget author
cannot specify a link relative to the widget itself. To allow this,
:program:`beng-proxy` can rewrite relative links to the following bases:

- ``template``: links are relative to the main template (default)

- ``widget``: links are relative to the widget; the browser will leave
  :program:`beng-proxy` if the user clicks on such a link, because it points to
  the widget server

- ``child``: link to a child widget; the URI is the ID of the child
  widget. You may append a relative URI separated by a slash.

- ``parent``: links are relative to the parent of this widget, i.e. the
  container which declared it

The base name must be specified in the element attribute ``c:base``
before the attribute containing the URI. To specify the mode of the
rewritten URI, you may use the attribute ``c:mode``:

.. _c_mode:

- ``direct``: direct link to the resource

- ``focus``: link to :program:`beng-proxy` serving the full page (or the current
  frame), focusing the widget (see :ref:`focus`)

- ``partial``: link to :program:`beng-proxy` serving only the selected widget;
  useful for frame contents

- ``response``: send a HTTP request to the widget and read the response
  body

The mode is ignored when the base is “``template``”.

The attribute ``c:view`` may be used to specify a view name.

:program:`beng-proxy` knows the following HTML elements, and optionally rewrites
URIs:

-  ``A``

-  ``AUDIO``

-  ``EMBED``

-  ``FORM``

-  ``IFRAME``

-  ``IMG``

-  ``SCRIPT``

-  ``VIDEO``

Example::

   <img c:base="widget" c:mode="partial" c:view="raw" src="foo.jpg"/>

Processing Instruction syntax
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To set a default value for all following link elements, you may use the
``<?cm4all-rewrite-uri?>`` XML Processing Instruction::

   <?cm4all-rewrite-uri c:base="widget" c:mode="focus"?>

This is recommended when many adjacent links share the same URI rewrite
settings, or when you cannot guarantee the order of attributes (many
XSLT processors mix the attribute order, which is allowed).

Absolute Widget Links
~~~~~~~~~~~~~~~~~~~~~

For widget with many nested levels of “directories”, it can become hard
to build a absolute links to its resources: a URI with a leading slash
is difficult to do, because that would require the widget code to know
where it was mounted; a relative link is as difficult, because it
requires the widget to be aware of the current nesting level, and needs
extra code.

To do that more easily, the tilde symbol may be used as a URI prefix:
the tilde followed by a slash is considered an absolute link pointing to
the root of the widget. Example:

Give a widget served from ``http://widget.server/foo/``, the URI
``~/bar.html`` always points to ``http://widget.server/foo/bar.html``.

This is a proprietary extension in the spirit of the UNIX shell syntax
(referring to the “home” of a widget). It does not work without
:program:`beng-proxy`.

.. _uriat:

Static Widget Resources
~~~~~~~~~~~~~~~~~~~~~~~

It is often desirable for widgets to publish static resource files in
a special global location, served without the processor overhead. This
location can be configured with the :ref:`LOCAL_URI <local_uri>`
translation packet.

Within a widget, the URI prefix ``@/`` refers to this
location. Example::

   <img src="@/logo.png"/>

All resources in this location are decoupled from the widget instance
and from the current document. Therefore, the URI rewriting mode is
ignored.

.. _frames:

Frames
------

:program:`beng-proxy` supports displaying widgets in an ``IFRAME`` or ``IMG``
element. To do this, declare your widget with ``display=none``. After
that, insert an ``IFRAME`` element (or any other element which
references its content with an URI), and let :program:`beng-proxy` rewrite the
URI::

   <c:widget id="post" type="demo_post" display="none"/>
   <iframe width="200" height="200" c:base="child"
     c:mode="partial" src="post"/>

This may be used for any HTML tag which is supported by the :program:`beng-proxy`
URI rewriting code, here an example for a widget rendering an image::

   <c:widget id="logo" type="logorenderer" display="none"/>
   <img c:base="child" c:mode="partial" c:view="raw" src="logo"
     alt="Our website logo"/>

Note that we use ``c:view=raw`` here (assuming a view with that name was
defined), because an image should not (and can not) be processed by
:program:`beng-proxy`. You can also use ``c:mode=direct`` if you want the browser
to request the resource from widget server directly instead of proxying
through :program:`beng-proxy`.

Untrusted Widgets
-----------------

Usually, widgets are embedded inside the one single HTML page. The
problem is that all scripts run with the same privileges, and each
widget’s scripts can access the whole page, each widget can invoke
requests to any other widget.

As a safeguard against potentially malicious widgets, :program:`beng-proxy` can
run widgets in a separate domain. The default security settings of
browsers will disallow cross-domain script access.

To make a widget class “untrusted”, the translation server generates the
``HOST`` packet with a host name for that widget. A host name may be
shared by a group of widget classes.

While translating a request, the translation server may send the
``UNTRUSTED`` packet, repeating the host name of the request. This makes
the request itself “untrusted”: trusted widgets are rejected, and only
those untrusted widgets matching the specified host name are accepted.
If the packet is absend, all untrusted widgets are rejected.

The Beng JavaScript API
=======================

JavaScript code in a widget frequently needs to send HTTP requests to
the widget server. All these requests must got through :program:`beng-proxy`.
Since the structure of a :program:`beng-proxy` URI is regarded internal, it
provides a JavaScript function to generate such an URI::

   function
   beng_widget_uri(base_uri, session_id, frame, focus, mode,
                   path, translate, view);

The return value is the URI which can be safely requested by the
widget server. For ``base_uri`` and, ``frame``, you should pass the
value of ``&c:base;``, ``&c:frame;``.  The ``session_id`` parameter is
obsolete and should be ``null``.

``focus`` is the path of the focused widget, and can be filled with
``&c:path;`` most of the time, unless you can to request a different
widget than the current one.

``mode`` is one of the following:

- ``focus``: the full page (the default if ``null`` is passed)

- ``partial``: just this one widget, processor enabled (must be
  ``text/html``)

The ``path`` argument is an URI relative to the widget. It may include a
query string.

The ``translate`` argument is passed to the translation server as
``PARAM`` packet.

``view`` is the name of the transformation view to use. This parameter
is ignored unless ``frame`` is set, or ``mode`` is “``partial``”.

.. _textprocessor:

The Text Processor
==================

The text processor expands the entity references described in
:ref:`entities`, but does nothing else. It may be useful to insert
values into JavaScript files.

.. _cssprocessor:

The CSS Processor
=================

The CSS processor is a transformation for cascading style sheets. The
translation server enables it with the packet ``PROCESS_CSS``. It is the
equivalent of the HTML processor for CSS: it can convert URLs to widget
resources. This allows proxying resources that are referenced in CSS.

The proprietary property ``-c-mode`` specifies the URL rewriting mode
for the following URLs in the current block. See :ref:`c:mode
<c_mode>` for a list of valid values. ``-c-mode`` configures a view
name. Example::

   body {
     -c-mode: partial;
     -c-view: raw;
     background-image:url('background.png');
   }

.. _options-1:

Options
-------

The following translation packets may be used to configure the CSS
processor:

- ``PROCESS_CSS``: Enables the CSS processor.

- ``PREFIX_CSS_CLASS``: CSS class names with leading underscore get a
  widget specific prefix, see below.

.. _prefix_css_class:

Local Classes
-------------

When the option ``PREFIX_CSS_CLASS`` is enabled, CSS class names with a
leading underscore are rewritten. The option is available in both
processors (HTML and CSS).

Two leading underscore makes the class local to the current widget
class. It may be shared by multiple instances of the same class. The
two underscores are replaced by the value of ``&c:class;`` (see
:ref:`entities`).

Three leading underscore makes the class local to the current widget
instance. The three underscores are replaced by the value of
``&c:prefix;`` (see :ref:`entities`). Each instance may define
different styles for this class.

The expansion is applied even when the class/id consists only of two or
three underscores.

.. _subst:

The Substitution Filter
=======================

This filter substitutes variables with their according values in a text
stream. It can be enabled with ``SUBST_YAML_FILE``.

Variable references have the form “``{%NAME%}``”. If
``SUBST_ALT_SYNTAX`` is specified, the syntax is instead switched to
“``{[NAME]}``”.

If a variable does not exist, the behavior is undefined; the reference
may be left as-is, or replaced with an empty string, or the filter may
fail with an error. Don’t make assumptions on what happens; it may
change at any time.

Multiple consecutive substitution filters may be merged. Thus, variable
values which contain another variable reference (or recursive variable
references) are not supported and the resulting behavior is undefined.
Duplicate variable names also result in undefined behavior.

Security Considerations
-----------------------

The values are inserted raw into the stream, i.e. without any
escaping/quoting. This has implications which need to be kept in mind.

If an attacker controls variable values, he may be able to inject
JavaScript or, more dangerously: if the substitution filter comes before
a XML processor, he may be able to inject widget instances. On the other
hand, if the substitution filter comes after the XML processor, variable
references in inline widgets will also be substituted, which may have
displeasing consequences.

The prototype translation server
================================

Until the ``jetserv`` daemon is finished, the prototype translation
server should be used. It is not configurable; this section describes
its hard-coded behaviour.

Request translation
-------------------

The document root is :file:`/var/www`. File names ending with ``.html`` are
mapped to the content type “text/html; charset=utf-8” and are marked
with the flags ``PROCESS``, ``CONTAINER``.

Widget registry
---------------

The translation server expects a file for each registered widget type
named :file:`/etc/cm4all/beng/widgets/TYPENAME`. Example::

   server "http://cfatest01.intern.cm-ag/date.py"
   process
   container

The first line is mandatory: it specifies the widget server. ``process``
enables the template processor; if that is not specified, the HTML
output is inserted into the resulting page verbatim. ``container``
allows the widget to embed sub widgets, ``stateful`` sets the “stateful”
flag.

Disabling features may increase the performance dramatically, because it
allows :program:`beng-proxy` to make better assumptions on data it does not know
yet. So if you know the widget is a leaf widget, do not specify
``container``.

Instead of ``server``, you can use ``cgi`` to specify the absolute path
of a CGI script which will serve the widget, or ``path`` for a static
widget.

For CGI widgets, you can also specify the options ``script_name``,
``document_root``, ``action``, ``interpreter``.
