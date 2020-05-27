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

Instead of referring to a previously defined node name, you can
configure an IP address instead, and :program:`beng-lb` creates a new node
implicitly.

The ``sticky`` setting specifies how a node is chosen for a request,
see :ref:`sticky` for details.

When all pool members fail, an error message is generated. You can
override that behaviour by configuring a “fallback”::

   pool demo {
     fallback "http://the.fallback.server/error.html"
     # ...
   }

This would generate a “320 Found” redirect to the specified URL. Another
type of fallback is a custom response, you can specify a HTTP status
code and a brief message (plain text)::

   pool demo {
     fallback "500" "Currently not available."
     # ...
   }

The option ``mangle_via yes`` enables request header mangling: the
headers ``Via`` and ``X-Forwarded-For`` are updated.

Zeroconf
~~~~~~~~

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

This requires ``avahi-daemon`` to be installed and running. And, of
course, it requires the pool members to publish their service.

If ``sticky`` is enabled on the pool, then :program:`beng-lb` uses
`consistent hashing
<https://en.wikipedia.org/wiki/Consistent_hashing>`__ to pick a member
(to reduce member reassignments).

With option ``sticky_cache`` set to ``yes``, consistent hashing is
disabled in favor of an assignment cache. The advantage of that cache
is that existing clients will not be reassigned when new nodes
appear. The major disadvantage is that this works only with a single
:program:`beng-lb` instance, and the cache is lost on restart. The
default is ``no``.

Protocols
~~~~~~~~~

The protocol ``tcp`` forwards raw a raw bidirectional TCP stream. It is
the fastest mode, and should be used when no special protocol parsing is
needed.

The protocol ``http`` means that :program:`beng-lb` parses the HTTP/1.1
request/response, and forwards them to the peer. This HTTP parser is
needed for some of the advanced features, such as cookies.

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
must be uniquie. Example::

   branch foo {
     goto pool1 if $request_method == "POST"
     goto pool2 if $request_uri =~ "^/for/pool2/"
     goto pool3 if $http_user_agent =~ "Opera"
     goto pool4
   }

The object contains any number of “goto” statements, all but the last
one with conditions. These “goto” statements can refer to a pool or
another branch.

The following “variables” are available:

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

The last token is a quoted string depicting the value to compare with,
or the regular expression.

Instead of ``goto``, you can use ``status`` or ``redirect`` to let
:program:`beng-lb` generate a brief response with the given HTTP status code or
``Location`` header::

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
- ``method``: the request method
- ``has_body``: ``true`` if a request body is present
- ``remote_host``: the client’s IP address

Read-only methods:

- ``get_header(name)``: Look up a request header. For performance
  reasons, the name must be lower-case.

The following methods can be used to generate a response:

- ``send_message(msg)``: Send a plain-text response.
- ``send_message(status, msg)``: Send a plain-text response with the
  given status.

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
blocked. It is very easy to make :program:`beng-lb` unusable with a Lua script.
Each Lua invocation adds big amounts of overhead. This feature is only
meant for development and quick’n’dirty hacks. Don’t use in production,
and if you really must do, take extreme care to make the Lua code finish
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

- ``bind``: an adddress to bind to. May be the wildcard ``*`` or an
  IPv4/IPv6 address followed by a port. IPv6 addresses should be
  enclosed in square brackets to disambiguate the port
  separator. Local sockets start with a slash :file:`/`, and abstract
  sockets start with the symbol ``@``.

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

- ``ssl``: ``yes`` enables SSL/TLS. See :ref:`ssl` for more
  information and more SSL options.

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
~~~~~~

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

Server Name Indication
~~~~~~~~~~~~~~~~~~~~~~

You can specify multiple ``ssl_cert`` lines. All certificate/key pairs
are loaded. During the TLS handshake, the client may announce the
desired server name with the TLS extension “SNI” (Server Name
Indication). :program:`beng-lb` uses this name to choose a certificate.
Currently, it uses the first matching certificate, but that algorithm
may be changed in the future to “most specific certificate”. If no
certificate matches, the first certificate is used.

.. _certdbconfig:

Certificate Database
~~~~~~~~~~~~~~~~~~~~

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
will be encrypted (“wrapped”) with the first AES256 key. That way,
private keys are not leaked to everybody with read acccess to the
database. Multiple ``wrap_key`` lines may be used to migrate to new AES
keys, while still being able to use private keys encrypted with an old
AES key. The database refers to AES keys by their name, which means you
must not rename the keys in the configuration file. A new AES key may be
generated using “``cm4all-certdb genwrap``”.

Each time a server name is received from a client, :program:`beng-lb` will
attempt to look up a matching certificate, and use that for the TLS
handshake.

See :ref:`certdb` for instructions on how to create and manage the
database.

Client Certificates
~~~~~~~~~~~~~~~~~~~

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

Note that due to technical limitations of the current implementation, it
is not possible to combine client certificate and the certificate
database.

Wireshark
~~~~~~~~~

Wireshark can decrypt SSL/TLS traffic if it knows the session keys.
These keys can be logged by :program:`beng-lb` and
:program:`beng-proxy` by setting the environment variable
:envvar:`SSLKEYLOGFILE` to the desired file name.  All keys will then
be appended to that file.  Obviously, this compromises the security of
all logged connections, so it should not be used on production servers
where real data is transmitted.

In Wireshark, you can specify the file in the SSL protocol settings as
"(Pre-)Master-Secret log filename".

.. note::
   This feature is only available if the project was built with
   OpenSSL 1.1.1 or newer (i.e. Debian Buster).

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
~~~~

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
~~~~~~~

The ``connect`` monitor attempts to establish a TCP connection, and
closes it immediately.

TCP Expect
~~~~~~~~~~

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

.. _certdb:

Certificate Database
====================

The certificate database is useful for bulk hosting. The database may
contain a dynamic list of X.509 server certificates.

See :ref:`certdbconfig` for instructions on how to configure the
database in :program:`beng-lb`.

Creating the Database
---------------------

Create a PostgreSQL database and run the file ``certdb.sql`` to create
the table.

Migrating the Database
----------------------

To update the database schema after an update, type::

   cm4all-certdb migrate

After the schema update, all users of the database should be updated
quickly to the same version. While it is attempted to keep read-only
backwards compatibility as much as possible, applications with write
access may cease to work after the migration.

Managing Certificates
---------------------

The program ``cm4all-certdb`` is a frontend for the database. It loads
the PostgreSQL connect string and the ``wrap_key`` settings from the
:program:`beng-lb` configuration file
(i.e. :file:`/etc/cm4all/beng/lb.conf`). The connect string can be
overridden from the one-line text file
:file:`/etc/cm4all/beng/certdb.connect`, just in case this
command-line tool needs a different setting.

Load a new certificate into the database::

   cm4all-certdb load cert.pem key.pem

Find a certificate for the given name::

   cm4all-certdb find www.example.com

Monitor database changes::

   cm4all-certdb monitor

Let’s Encrypt
-------------

The ``cm4all-certdb`` program includes an ACME (Automatic Certificate
Management Environment) client, the protocol implemented by the *Let’s
Encrypt* project.

To get started, register an account::

   openssl genrsa -out /etc/cm4all/acme/account.key  4096
   cm4all-certdb acme --staging new-reg foo@example.com

This prints the account "location", which must be specified with the
``--account-key-id`` option in every successive call.  To look up the
location of an existing account, type::

  cm4all-certdb acme --staging get-account

Note: examples listed here will use the “staging” server. Omit the
``–-staging`` option to use the Let’s Encrypt production server.

To obtain a signed certificate, type::

   cm4all-certdb acme --staging \
     --challenge-directory /var/www/acme-challenge \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     new-order example www.example.com example.com foo.example.com

To update all names in an existing certificate, use the command
``renew-cert`` and specify only the handle (``example`` here)::

   cm4all-certdb acme --staging \
     --challenge-directory /var/www/acme-challenge \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     renew-cert example

This requires that the URL
``http://example.com/.well-known/acme-challenge/`` maps to the
specified ``--challenge-directory`` path (on all domains).

After the program finishes, the new certificate should be usable
immediately.

Wildcards
~~~~~~~~~

To obtain a certificate for a wildcard, the ACME client needs to use
DNS-based authorization (``dns-01``) instead of HTTP-based
(``http-01``).  Use the command-line option ``--dns-txt-program`` to
specify a program which updates the ``TXT`` record of an ACME
challenge host::

   cm4all-certdb acme --staging \
     --dns-txt-program /usr/lib/cm4all/bin/set-acme-challenge-dns-txt \
     --account-key-id https://staging.api.letsencrypt.org/acme/acct/42 \
     new-order example *.example.com

This program is invoked twice: once to set a ``TXT`` record and again
to delete the ``TXT`` record after finishing authorization.  It
accepts the following parameters:

1. the full-qualified DNS host name (the program shall prepend the
   prefix ``_acme-challenge.``)
2. ``TXT`` record values

All ``TXT`` records but the given ones are removed.  If given just the
DNS host name and no ``TXT`` record value, then all existing ``TXT``
records are deleted.
