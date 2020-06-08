.. _log:

Logging Protocol
################

By default, accesses to HTTP resources are logged into the standard
log file (command-line option ``--logger``). The logging protocol
offers a more flexible alternative: a child process is launched,
connected to ``beng-proxy`` with a datagram socket. Each datagram
describes an event to be logged.

Launching
---------

On startup, the logging process has a datagram socket on file descriptor
0. On this socket, it receives packets describing the event. File
descriptor 2 is connected with the local error log, and can be used to
print fatal error messages.

Datagram Format
---------------

Each event is serialized into exactly one datagram. That puts a limit on
the size of an event, and therefore, this protocol is designed to be
small but still easy enough to parse.

A datagram begins with the number 0x63046102 (32 bit), and is followed
by one or more attributes. The first byte of each attribute is the
attribute id (see ``enum beng_log_attribute`` in ``beng-proxy/log.h``).
What follows is specific to the attibute id.

General rules:

-  there is no padding

-  all numbers are in network byte order (big endian)

-  strings are terminated by a null byte

Configuring
-----------

The configuration block ``access_logger`` configures the access logger.
Example which launches an external logger program::

   access_logger {
     shell "exec cm4all-beng-proxy-log-split /tmp/%{year}%{month}.log"
   }

The specified command is executed with the shell (``/bin/sh -c``).

Another example sends log datagrams via UDP to a multicast address::

   access_logger {
     send_to "ff02::dead:beef"
   }

The following example disables access logging completely::

   access_logger {
     enabled "no"
   }

The default is to log to the journal.

The following ``access_logger`` options are available:

- ``enabled``: “no” disables access logging completely.

- ``send_to``: Send log datagrams to the specified IP address, which
  may be multicast.

- ``shell``: Execute an external access logger program as child
  process. The specified command is execute with the shell
  (``/bin/sh -c``).

- ``trust_xff``: The specified IP address is a “trusted” proxy whose
  ``X-Forwarded-For`` value is used to determine the ``remote_host``.
  This option can be specified multiple times.

- ``ignore_localhost_200``: The value is an URI which is never logged
  if the ``Host`` header is “localhost” and the response status is
  ``200 OK``.

- ``forward_child_errors``: “yes” forwards error messages from child
  processes (``stderr``) to the logger (and not to the local journal).

- ``child_error_rate_limit``: Limit the rate of error messages from
  child processes.  Expects two values: the number of lines per
  second, and the burst count.

- ``child_error_is_default``: :samp:`no` disables
  ``forward_child_errors`` by default, and allow the translation
  server to enable it for each child process with the ``STDERR_POND``
  packet (see :ref:`childoptions`).

.. _child_error_logger:

Child Error Logger
~~~~~~~~~~~~~~~~~~

To be able to use the ``forward_child_errors`` feature without logging
HTTP requests, the section ``child_error_logger`` can be used
instead::

   child_error_logger {
     send_to "ff02::dead:beef"
   }

This section contains either ``send_to`` or ``shell``, as described in
the previous section.

The option ``child_error_rate_limit`` must be called just
``rate_limit`` in this section, and ``child_error_is_default`` becomes
``is_default``.

Included Loggers
----------------

This section describes the loggers which are included in the Debian
package ``cm4all-beng-proxy-logging``.

``log-cat``
~~~~~~~~~~~

Prints the events to standard output, which will be written to
``beng-proxy``\ ’s error log file (as if you had not configured a
logger). It has no arguments.

You can combine it with ``multilog`` or similar programs, for example::

   cm4all-beng-proxy-log-cat |multilog t /var/log/cm4all/access

``log-json``
~~~~~~~~~~~~

Prints the events to standard output in JSON format. It has no
arguments.

``log-lua``
~~~~~~~~~~~

Run a Lua function for each request. Example code::

   function access_log(r)
      print(r.http_method, r.http_uri)
   end

The function receives one parameter: the request object. The following
attributes are available (if they were received from the web server):

- ``type``: The record type. Can be ``"http_status"``,
  ``"http_error"``, ``"submission"`` or ``null`` if no type was
  specified.

- ``logger_client``: The address of the entity from which the access
  log datagram was received.

- ``timestamp``: The time stamp of the request in seconds since epoch.

- ``remote_host``: The address of the remote host as a string.

- ``forwarded_to``: The (string) address of the host (including port
  number if applicable) which this request has been forwarded to.

- ``host``: The "Host" request header.

- ``site``: The name of the site which was accessed.

- ``http_method``: The request method as an all-upper-case string.

- ``http_uri``: The request URI.

- ``http_referer``: The "Referer"[sic] request header.

- ``user_agent``: The "User-Agent" request header.

- ``http_status``: The response status as an integer.

- ``length``: The netto length of the response body in bytes.

- ``traffic_received``: The total number of raw bytes received. This
  includes all extra data such as headers.

- ``traffic_sent``: The total number of raw bytes sent. This includes
  all extra data such as headers.

- ``duration``: The wallclock duration of the operation in seconds.

- ``message``: An opaque one-line message (without a trailing newline
  character). This is used for error logging, not for HTTP access
  logging.

The Lua logger can also be used to filter requests being forwarded to
the next logger::

   cm4all-beng-proxy-log-lua logger.lua filterfunc \
       --filter-exec cm4all-beng-proxy-log-json

This loads ``logger.lua`` and calls the function ``filterfunc`` for each
request. If the function returns ``true``, then the request is forwarded
to the ``cm4all-beng-proxy-log-json`` process.

Example filter program::

   function loggerfunc(r)
      return r.http_uri == '/wp-login.php'
   end

As a shortcut, a Lua code fragment can be passed on the command line::

   cm4all-beng-proxy-log-lua \
       --handler-code 'return _.http_uri == "/wp-login.php"' \
       --filter-exec cm4all-beng-proxy-log-json

The code fragment is called for each request. The global variable ``_``
(underscore) contains the request object. The code fragment should
contain a ``return`` statement.

``log-traffic``
~~~~~~~~~~~~~~~

Print site traffic to standard output. Each line is in the form
“``SITENAME TRAFFICBYTES``”.

``log-split``
~~~~~~~~~~~~~

Splits the events into several log files. The parameters are format
strings which are used to build the file name. The first valid format
string is used. Variables in the form ``%{name}`` are substituted; a
format string is invalid if an unknown or undefined variable is
referenced. If no valid format string is valid for an event, nothing is
logged.

Directories are auto-created if they do not exist.

The following variables are available:

- ``date``: the date in the form YYYY-mm-dd
- ``year``: the year (4 digits)
- ``month``: the month (01..12)
- ``day``: the day of month (01..31)
- ``hour``: the hour (00..23)
- ``minute``: the minute (00..59)
- ``site``: the name of the “site”

Example::

   cm4all-beng-proxy-log-split \
       /var/log/per-site/%{site}/%{date}.log \
       /var/log/unknown-site/%{year}.log

If the first argument is ``–localtime``, then local time is used instead
of GMT.

``log-forward``, ``log-exec``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``log-forward`` forwards the events via UDP to a remote host. The
parameters are the IP addresses of the peers (there may be more than
one).  Example::

   cm4all-beng-proxy-log-forward 192.168.1.133

IPv6 addresses may come with a scope id, either numeric or the interface
name, which is useful/necessary for link-local or multicast addresses::

   cm4all-beng-proxy-log-forward fe80::42%1
   cm4all-beng-proxy-log-forward fe80::42%eth0

``log-exec`` listens on a UDP port, and launches the real logger bound
to it::

   daemon -o /var/log/access.log \
       cm4all-beng-proxy-log-exec '*' cm4all-beng-proxy-log-cat

The first parameter is the IP address to bind to; “\*” means listen on
all interfaces. The option “``–multicast-group``” can be used to join a
multicast group.

These two programs are useful in conjunction, to store logs on a central
server.

Multicast example
~~~~~~~~~~~~~~~~~

Multicast can be used to send access logs to one or multiple loggers at
the same time, without having to configure them manually. If the senders
and the receivers are on the same network, this usually works without
extra configuration.

First, you need to choose a multicast group address. Usually, you should
pick an address from the link-local network, which is ``ff02::/16`` for
IPv6.

This example launches a receiver::

   cm4all-beng-proxy-log-exec --multicast-group ff02::dead:beef :: \
     cm4all-beng-proxy-log-cat

The following :program:`beng-proxy` command-line option will send its access log
to all listening loggers:
``–access-logger ’./build/cm4all-beng-proxy-log-forward ff02::beef’``


``log-tee``
~~~~~~~~~~~

``log-tee`` launches multiple child loggers given on the command line
and copies events to all of them.  Example::

   cm4all-beng-proxy-log-tee \
     "cm4all-beng-proxy-log-cat |multilog t /var/log/cm4all/access" \
     "cm4all-beng-proxy-log-forward 192.168.1.33"
