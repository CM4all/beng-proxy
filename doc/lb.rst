beng-lb
#######

:program:`beng-lb` is a light-weight HTTP load balancer.


Features
========

:program:`beng-lb` listens on a number of socket, and forwards the
requests to another server. Behind one listener socket, more than one
cluster node may be configured.

The algorithm for selecting a cluster node is configurable: it may be
pure load balancing, pure failover, stickiness with the
:program:`beng-proxy` session cookie, or stickiness with a
:program:`beng-lb` cookie.

Installation
============

:program:`beng-lb` requires a current Debian operating system.

Install the package :file:`cm4all-beng-lb`. Edit the file
:file:`/etc/cm4all/beng/lb.conf`. Finally, restart
:program:`beng-lb`::

  systemctl restart cm4all-beng-lb

Configuration
=============

Configuration is read from the file :file:`/etc/cm4all/beng/lb.conf`. From
there, other files can be included with the ``@include`` keyword::

   @include "foo/bar.conf"
   @include_optional "foo/may-not-exist.conf"

The second line silently ignores non-existing files.

The following object types can be configured here:

- nodes

- pools

- listeners

Variables (``@set``)
--------------------

Set a variable. Within double-quoted strings, variables can be expanded
with ``${name}``.  Example::

   @set foo = "192.168.1.42"
   @set bar = "${foo}:80"
   listener {
     bind "${bar}"
   }

At the time of this writing, the concept of variables is not
well-implemented. For example, (backslash) escape sequences don’t work,
and the scope of variables is not defined. For now, use variables only
for very simple things.

Nodes
-----

A “node” is one server in a cluster. It handles requests. The node has
an IP address, but no port/service. A node can have multiple services,
it can be member in multiple pools. Technically, a physical server can
have multiple nodes by having more than one IP address, but that is only
a side effect and is not useful in practice.

Example::

   node localhost {
     address "127.0.0.1"
   }

Pools
-----

A pool is a cluster of nodes for a specific service. When a request
needs to be handled, the pool chooses a member and sends the request to
this node. Upon failure, it may repeat the request with a different
node.

Example::

   pool demo {
     protocol "http"
     member "foo:http"
     member "bar:http"
   }

- ``name``: the name of the pool (can also be specified on the
  ``pool`` line).

- ``protocol``: ``tcp`` or ``http``; see :ref:`lb_protocol`.

- ``ssl``: use HTTPS (HTTP over SSL/TLS) instead of plain HTTP for
  outgoing connections to members.

- ``hsts``: ``yes`` generates a ``Strict-Transport-Security`` header
  in the first response of each connection.

- ``http_host``: overrides the ``Host`` header in all forwarded HTTP
  requests.  This also enables TLS Server Name Indication (RFC 6066
  3).

- ``sticky``: specify how a node is chosen for a request,
  see :ref:`sticky` for details.

- ``sticky_method``: one of:

  - ``consistent_hashing`` (the default)

  - ``rendezvous_hashing``

  - ``cache``: an assignment cache. The advantage of that cache is that
    existing clients will not be reassigned when new nodes appear. The
    major disadvantage is that this works only with a single
    :program:`beng-lb` instance, and the cache is lost on restart.

- ``session_cookie``: the name of the session cookie for
  ``sticky session_modulo``.

- ``sticky_hex_uuid_uri_prefix``: if the request URI begins with this
  prefix followed by a UUID or 32 lower-case hex digits (i.e. a UUID
  without hyphens), then the ``sticky``
  setting will be ignored and instead a UUID will be generated from
  those 32 hex digits (by inserting four dashes).  This is an
  experimental feature and may change or be removed at any time.

- ``monitor``: the name of a monitor which shall be used to check this
  pool's members; see :ref:`monitors`.

- ``member``: each ``member`` line adds a static member.  Instead of
  referring to a previously defined node name, you can configure an IP
  address instead, and :program:`beng-lb` creates a new node
  implicitly.

- ``zeroconf_service``, ``zeroconf_domain``, ``zeroconf_interface``,
  ``zeroconf_protocol``: automatically discover members; see
  :ref:`lb_zeroconf`.

- ``fair_scheduling``: if ``yes``, enables fair scheduling, which
  attempts to schedule HTTP requests for different sites in a way that
  avoids one site blocking all backend connections.

- ``tarpit``: if ``yes``, delays clients which send many consecutive
  HTTP requests, in order to mitigate DDoS attacks.

- ``source_address``: see :ref:`transparent`.

- ``mangle_via``: if ``yes``, enables request header mangling: the
  headers ``Via`` and ``X-Forwarded-For`` are updated.

- ``fallback``: what to do when all pool members fail; see
  :ref:`fallback`.

.. _fallback:

Fallback
^^^^^^^^

When all pool members fail, an error message is generated. You can
override that behaviour by configuring a “fallback”::

   pool demo {
     fallback "http://the.fallback.server/error.html"
     # ...
   }

This would generate a "302 Found" redirect to the specified URL. Another
type of fallback is a custom response, you can specify a HTTP status
code and a brief message (plain text)::

   pool demo {
     fallback "500" "Currently not available."
     # ...
   }

.. _lb_zeroconf:

Zeroconf
^^^^^^^^

To discover pool members automatically using Zeroconf, use the
``zeroconf_service`` setting::

   pool "auto" {
      zeroconf_service "widgetserver"
   }

It will look up in the default domain; to use another domain, use the
``zeroconf_domain`` setting.

To limit the search to a certain network interface, specify a
``zeroconf_interface``::

   pool "auto" {
      zeroconf_service "widgetserver"
      zeroconf_interface "eth1"
   }

By default, both IPv4 and IPv6 is used.  To limit the protocol,
configure either ``zeroconf_protocol inet`` or ``zeroconf_protocol
inet6``.

This requires ``avahi-daemon`` to be installed and running. And, of
course, it requires the pool members to publish their service.

If ``sticky`` is enabled on the pool, then :program:`beng-lb` uses
`consistent hashing
<https://en.wikipedia.org/wiki/Consistent_hashing>`__ to pick a member
(to reduce member reassignments).

.. _lb_protocol:

Protocols
^^^^^^^^^

The protocol ``tcp`` forwards raw a raw bidirectional TCP stream. It is
the fastest mode, and should be used when no special protocol parsing is
needed.

The protocol ``http`` means that :program:`beng-lb` parses the HTTP/1.1
request/response, and forwards them to the peer. This HTTP parser is
needed for some of the advanced features, such as cookies.

.. _transparent:

Transparent Source IP
---------------------

The ``source_address`` setting can be used on TCP pools to forward the
connection transparently with its original source IP. Example
configuration::

   pool demo {
     protocol "tcp"
     source_address "transparent"
     member "foo:1234"
     member "bar:1234"
   }

Note that this requires a routing table on the pool members that routes
reply packets back to :program:`beng-lb` instead of replying to the client
directly. The easiest way to do it is make the :program:`beng-lb` server the
default gateway. If that is not desirable, advanced routing with
Netfilter marks are a powerful alternative.

Conditional Pools
-----------------

Incoming requests can be delivered to pools based on
administrator-specified conditions. This virtual pool is called
``branch``, and can be referenced by its name just like regular pools.
That implies that pools and branches share a namespace, their names
must be unique. Example::

   branch foo {
     status 418 if $remote_address in "192.168.0.0/16"
     goto pool1 if $request_method == "POST"
     goto pool2 if $request_uri =~ "^/for/pool2/"
     goto pool3 if $http_user_agent =~ "Opera"
     goto pool4
   }

The object contains any number of “goto” statements, all but the last
one with conditions. These “goto” statements can refer to a pool or
another branch.

The following “variables” are available:

- ``$remote_address``: the client address.
- ``$peer_subject``, ``$peer_issuer_subject``: the subject of the
  (issuer of the) client certificate (see :ref:`ssl_verify`).
- ``$request_method``: the HTTP request method (``GET``, ``POST``,
  ...)
- ``$request_uri``: the HTTP request URI
- ``$http_header_name``: a HTTP request header with the specified
  name, lower case, dashes converted to underscore

The following operators are available:

- ``==``: check the value for equality
- ``!=``: check the value for non-equality
- ``=~``: Perl-compatible regular expression match
- ``!~``: Perl-compatible regular expression mismatch

For ``$remote_address``, only the following operators are available:

- ``in``: check whether the address matches the given address with
  prefix length, e.g. ``$remote_address in "192.168.0.0/16"``
- ``not in``: negated version of ``in``

The last token is a quoted string depicting the value to compare with,
or the regular expression.

Instead of ``goto``, you can use ``status``, ``redirect`` or
``redirect_https`` to let :program:`beng-lb` generate a brief response
with the given HTTP status code or ``Location`` header::

   branch foo {
     status 418 if $http_user_agent =~ "BadBot"
     redirect "http://www.cm4all.com/" if $http_user_agent =~ "Foo"
     goto bar
   }

At the time of this writing, conditional pools work only for HTTP, not
for TCP.

Lua Request Handlers
--------------------

HTTP requests can be handled by Lua scripts. The ``lua_handler`` section
declares such a request handler::

   lua_handler "my_lua_handler" {
     path "test.lua"
     function "handle_request"
   }

The name ``my_lua_handler`` can be referred to by a ``listener``\ ’s
``pool`` setting.

A simple Lua script may look like this::

   function handle_request(r)
      r:send_message("Hello world")
   end

The configured function receives one parameter: the request object. The
following attributes are available:

- ``uri``: the request URI
- ``query_table``: a table containing all (unique) query parameters
- ``method``: the request method
- ``has_body``: ``true`` if a request body is present
- ``remote_host``: the client’s IP address
- ``peer_subject``, ``peer_issuer_subject``: the subject of the
  (issuer of the) client certificate (see :ref:`ssl_verify`)

Read-only methods:

- ``get_header(name)``: Look up a request header. For performance
  reasons, the name must be lower-case.

The following methods can be used to generate a response:

- ``send_message(msg)``: Send a plain-text response.
- ``send_message(status, msg)``: Send a plain-text response with the
  given status.

- ``send_redirect([status, ] location [, msg])``: Send a HTTP redirect
  with the specified ``Location`` header.  The default HTTP status can
  be overridden and a ``text/plain`` message can be specified.

- ``send_redirect_host([status, ] host [, msg])``: Like
  ``send_redirect()``, but replace only the host part of the current
  request URI.  If the URI path should remain the same, this is both
  easier to use and faster.

To forward the HTTP request to a configured pool, the Lua script
should look up that pool in the ``pools`` table (which is, for
performance reasons, only available during global initialization), and
then return that pool from the handler function, e.g.::

   foo = pools['foo']
   function handle_request(r)
      return foo
   end

The ``pools`` table contains all configured ``pool``, ``branch`` and
``lua_handler`` sections. This means that Lua code can direct the HTTP
request into a ``branch`` or into another Lua script. Be careful to
avoid loops!

During development, it may be convenient to forward HTTP requests to
dynamic workers (**never** use this feature on a production server,
because it may cause severe performance problems)::

   foo = pools['foo']
   function handle_request(r)
      return r:resolve_connect('server.name:8080')
   end

Caution: while a Lua script runs, the whole :program:`beng-lb` process is
blocked. It is very easy to make :program:`beng-lb` unusable with a
Lua script.  Take extreme care to make the Lua code finish
quickly.

Translation Request Handlers
----------------------------

This handler asks a translation server which pool shall be used to
handle a HTTP request (see :ref:`pooltrans`). The
``translation_handler`` section declares such a request handler::

   translation_handler "my_translation_handler" {
     connect "@translation"
     pools "a" "b" "c"
   }

The ``pools`` line specifies the pools (or branches or Lua handlers ...)
which may be chosen from.

Prometheus Exporters
--------------------

The ``prometheus_exporter`` section declares a request handler which
returns statistics for consumption by a `Prometheus
<https://prometheus.io/>`_ server.::

  prometheus_exporter "prometheus" {
    load_from_local "/run/cm4all/prometheus-exporters/cgroup.socket"
    load_from_local "/run/cm4all/prometheus-exporters/process.socket"
  }

The following options are available:

- ``load_from_local``: connect to this local socket (absolute path or
  abstract socket prefixed by ``@``), send HTTP request and append the
  response.

Prometheus HTTP Service Discovery
---------------------------------

The ``prometheus_discovery`` section translates services discovered
via Zeroconf to a `Prometheus HTTP Service Discovery
<https://prometheus.io/docs/prometheus/latest/http_sd/>`__ JSON
response.  Example configuration::

  prometheus_discovery "pd" {
    zeroconf_service "prometheus"
    zeroconf_protocol "inet"
  }

See :ref:`lb_zeroconf` for a list of available options.

Listener
--------

A listener is a socket address (IP and port) which accepts incoming TCP
connections::

   listener port80 {
     bind "*:80"
     tag "foo"
     pool "demo"
   }

A listener has a name, a socket address to bind to (including the port).
To handle requests, it is associated with exactly one pool.

Known attributes:

- ``tag``: an optional tag, to be passed to the translation server in
  a ``LISTENER_TAG`` packet (if a translation server is ever queried
  during a HTTP request).

- ``zeroconf_service``: if specified, then register this listener as
  Zeroconf service in the local Avahi daemon.

- ``zeroconf_domain`` (optional): The name of the Zeroconf domain.

- ``zeroconf_interface``: publish the Zeroconf service only on the
  given interface.

- ``zeroconf_protocol`` (optional): Publish only protocol ``inet`` or
  ``inet6``.

- ``zeroconf_weight``: publish the Zeroconf service with the specified
  "weight", i.e. ask :program:`beng-lb` to use this weight when
  choosing nodes (works only with ``rendezvous_hashing``).  The value
  is a decimal number; the implied default value is :samp:`1.0`.  For
  example, if you specify :samp:`0.5`, you expect this node to get
  only half as many requests as others.

- ``bind``: an adddress to bind to. May be the wildcard ``*`` or an
  IPv4/IPv6 address followed by a port. IPv6 addresses should be
  enclosed in square brackets to disambiguate the port
  separator. Local sockets start with a slash :file:`/`, and abstract
  sockets start with the symbol ``@``.

- ``interface``: limit this listener to the given network interface.

- ``mode``: for local socket files, this specifies the octal file
  mode.

- ``mptcp``: ``yes`` enables Multi-Path TCP

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

- ``ssl``: ``yes`` enables SSL/TLS. See :ref:`ssl` for more
  information and more SSL options.

- ``redirect_https``: ``yes`` redirects all incoming HTTP requests to
  the according ``https://`` URL.  This can be specified instead of
  ``pool``.

- ``hsts``: ``yes`` generates a ``Strict-Transport-Security`` header
  in the first response of each connection.

- ``max_connections_per_ip``: specifies the maximum number of
  connections from each IP address.

- ``access_logger``: ``no`` disables the access logger on this
  listener.  A value other than ``yes`` or ``no`` selects a named
  ``access_logger`` block (see :ref:`log`).

- ``access_logger_only_errors``: ``yes`` limits the access log to
  failed requests (HTTP status 4xx and 5xx).

.. _client_ban_list:

- ``client_ban_list``: ``yes`` enables lookup in the client ban list
  which can be edited by control commands :ref:`REJECT_CLIENT
  <reject_client>` and :ref:`TARPIT_CLIENT <tarpit_client>`.

- ``verbose_response``: ``yes`` exposes internal error messages in
  HTTP responses.

.. _sticky:

Sticky
------

A pool’s ``sticky`` setting specifies how a node is chosen for a
request. Example::

   pool demo {
     protocol "http"
     member "foo:http"
     member "bar:http"
     sticky "failover"
   }

Requests to this pool are always sent to the node named “foo”. The
second node “bar” is only used when “foo” fails.

Other sticky modes:

- ``none``: simple round-robin (the default mode)

- ``failover``: the first non-failing node is used

- ``source_ip``: the modulo of the client’s source IP is used to
  calculate the node

- ``host``: the hash of the ``Host`` request header (or the
  ``CANONICAL_HOST`` translation packet) is used to calculate the node

- ``xhost``: the hash of the ``X-CM4all-Host`` request header is used
  to calculate the node

- ``session_modulo``: the modulo of the :program:`beng-proxy` session
  is used to calculate the node

- ``cookie``: a random cookie is generated, and the node is chosen
  from the cookie that is received from the client

- ``jvm_route``: Tomcat’s JSESSIONID is parsed, and its suffix is
  compared against the ``jvm_route`` of all member nodes

Tomcat
^^^^^^

For the ``jvm_route`` mode, both :program:`beng-lb` and Tomcat must be configured
properly. Example ``lb.conf``::

   node first {
      address 192.168.1.101
      jvm_route jvm1
   }

   node second {
      address 192.168.1.102
      jvm_route jvm2
   }

   pool demo {
     protocol "http"
     member "second:http"
     member "second:http"
     sticky "jvm_route"
   }

Example ``server.xml`` on the “first” Tomcat::

   <Engine name="Catalina" defaultHost="localhost" jvmRoute="jvm1">

The ``jvmRoute`` settings must match in :program:`beng-lb` and Tomcat. It is
allowed to set ``jvm_route`` in a node that is used in pools without the
according ``sticky`` setting.

.. _ssl:

SSL/TLS
-------

To enable SSL/TLS on a listener, configure::

   listener ssl {
     bind "*:443"
     pool "demo"
     ssl "yes"
     ssl_cert "/etc/cm4all/beng/lb/cert.pem" "/etc/cm4all/beng/lb/key.pem"
   }

One pool can be shared by listeners with and without SSL.

The certificate file specified by ``ssl_cert`` should contain the
whole certificate chain that is supposed to be transferred to the
client.

To indicate that an HTTP request was received on a SSL/TLS connection,
:program:`beng-lb` adds the ``X-CM4all-HTTPS:on`` header.

Server Name Indication
^^^^^^^^^^^^^^^^^^^^^^

You can specify multiple ``ssl_cert`` lines. All certificate/key pairs
are loaded. During the TLS handshake, the client may announce the
desired server name with the TLS extension “SNI” (Server Name
Indication). :program:`beng-lb` uses this name to choose a certificate.
Currently, it uses the first matching certificate, but that algorithm
may be changed in the future to “most specific certificate”. If no
certificate matches, the first certificate is used.

.. _certdbconfig:

Certificate Database
^^^^^^^^^^^^^^^^^^^^

Instead of configuring each server certificate in the configuration
file, you can store certificate/key pairs in a PostgreSQL database. The
``listener`` option ``ssl_cert_db`` specifies the symbolic name of a
``cert_db`` section::

   cert_db foo {
     connect "dbname=lb"
     ca_cert "/etc/cm4all/beng/lb/ca1.pem"
     ca_cert "/etc/cm4all/beng/lb/ca2.pem"
     # ...
     wrap_key "foo" "0123456789abcdef..."
     # ...
   }

   listener ssl {
     bind "*:443"
     pool "demo"
     ssl "yes"
     ssl_cert "/etc/cm4all/beng/lb/cert.pem" "/etc/cm4all/beng/lb/key.pem"
     ssl_cert_db "foo"
   }

There must be at least one regular ``ssl_cert``.

The ``cert_db`` section may contain any number of ``ca_cert`` lines,
each specifying a CA certificate chain file in PEM format. Each
certificate loaded from the database will be accompanied with the chain,
if a matching one was found.

The ``connect`` setting contains a PostgreSQL connect string.
Optionally, you may specify a non-standard PostgreSQL schema with the
``schema`` setting. Note that you need to allow the configured
PostgreSQL user to access the schema using
``GRANT USAGE ON SCHEMA TO username``.

If at least one ``wrap_key`` setting is present, all new private keys
will be encrypted (“wrapped”) with the first wrap key (a 256 bit
XSalsa20/Poly1305 key).  That way, private keys are not leaked to
everybody with read acccess to the database.  Multiple ``wrap_key``
lines may be used to migrate to new wrap keys, while still being able
to use private keys encrypted with an old wrap key.  The database
refers to wrap keys by their name, which means you must not rename the
wrap keys in the configuration file.  A new wrap key may be generated
using “``cm4all-certdb genwrap``”.

Each time a server name is received from a client, :program:`beng-lb` will
attempt to look up a matching certificate, and use that for the TLS
handshake.

See :ref:`certdb` for instructions on how to create and manage the
database.

.. _ssl_verify:

Client Certificates
^^^^^^^^^^^^^^^^^^^

The option ``ssl_verify`` enables client certificates. A connection
without a client certificate will be rejected. The client certificate
will only be accepted if its issuer can be validated by the server. By
default, the CA certificates in :file:`/etc/ssl/certs/` are used for this.
You can specify a custom CA file with the option ``ssl_ca_cert``, which
refers to a PEM file containing one or more concatenated acceptable CA
certificates. Example::

   listener ssl {
     bind "*:443"
     pool "demo"
     ssl "yes"
     ssl_cert "/etc/cm4all/beng/lb/cert.pem" "/etc/cm4all/beng/lb/key.pem"
     ssl_verify "yes"
     ssl_ca_cert "/etc/cm4all/beng/lb/ca.pem"
   }

The subject of the client certificate is copied to the web server in the
``X-CM4all-BENG-Peer-Subject`` request header, and its issuer uses the
``X-CM4all-BENG-Peer-Issuer-Subject`` request header.

By setting ``ssl_verify`` to “optional”, the connection will not be
rejected if the client chooses not to send a certificate, and the
headers described above will not be present. A client using an untrusted
certificate will still be rejected.

Wireshark
^^^^^^^^^

Wireshark can decrypt SSL/TLS traffic if it knows the session keys.
These keys can be logged by :program:`beng-lb` and
:program:`beng-proxy` by setting the environment variable
:envvar:`SSLKEYLOGFILE` to the desired file name.  All keys will then
be appended to that file.  Obviously, this compromises the security of
all logged connections, so it should not be used on production servers
where real data is transmitted.

In Wireshark, you can specify the file in the SSL protocol settings as
"(Pre-)Master-Secret log filename".

.. _monitors:

Monitors
--------

A “monitor” describes how the availability of nodes in a pool is checked
periodically. By default, there is no monitor, just a list of
“known-bad” nodes, filled with failures.

The option “interval” configures how often the monitor is executed (in
seconds).

The option “timeout” specifies how long :program:`beng-lb` waits for a response
(in seconds).

Check
-----

The section ``global_http_check`` can be used to intercept a certain
host/URI combination for monitors of load balancers in front of
:program:`beng-lb`::

   global_http_check {
     uri "/aLiVeChEcK.sErViCe"
     host "localhost"
     client "192.168.1.0/24"
     client "2003:abcd::/32"
     file_exists "/run/cm4all/beng-lb.alive"
     success_message "webisonline"
   }

All requests on all HTTP listeners matching the given URI and Host
request header will divert into a special handler which checks the
existence of the given file. If it exists, then the configured message
is emitted. If not, then an error is emitted.

The ``client`` setting is optional. If at least one is given, then only
the specified client addresses / networks are diverted to this handler.

Ping
^^^^

The “ping” monitor periodically sends echo-request ICMP packets to the
node, and excepts echo-reply ICMP packets.  Example::

   monitor "my_monitor" {
     type "ping"
     interval "2"
   }

   pool "demo" {
     member "...
     monitor "my_monitor"
   }

This requires Linux kernel 3.0 or newer.  :program:`beng-lb` must be
allowed to use the ICMP socket, which can be configured in the virtual
file :file:`/proc/sys/net/ipv4/ping_group_range`

Connect
^^^^^^^

The ``connect`` monitor attempts to establish a TCP connection, and
closes it immediately.

TCP Expect
^^^^^^^^^^

The ``tcp_expect`` monitor opens a TCP connection, optionally sends some
data, and expects a certain string in the response.  Example::

   monitor "expect_monitor" {
     type "tcp_expect"
     send "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
     expect "HTTP/1.1 200 OK"
   }

The ``send`` setting is optional.

The ``expect_graceful`` setting can be used for graceful shutdown::

   monitor "expect_monitor2" {
     type "tcp_expect"
     send "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"
     expect "HTTP/1.1 200 OK"
     expect_graceful "HTTP/1.1 503 Service Unavailable"
   }

If the configured string is received, the node will only receive
“sticky” requests, but no new sessions.

In addition to the generic total ``timeout`` setting, the setting
``connect_timeout`` can be used to limit the time for the TCP connect.

``control``
-----------

See :ref:`config.control`.

Access Loggers
--------------

The configuration block ``access_logger`` configures the access
logger.  See :ref:`log`.


``set``
-------

Tweak global settings. Most of these are legacy from the old ``–set``
command-line option.  Do not confuse with ``@set``, which sets
configuration parser variables!  Syntax::

   set NAME = "VALUE"

The following settings are available:

- ``tcp_stock_limit``: The maximum number of outgoing TCP connections
  per remote host.  0 means unlimited, which has shown to be a bad
  choice, because many servers do not scale well.
