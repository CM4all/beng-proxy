.. _control:

Remote Control Protocol
=======================


:program:`beng-proxy` can listen for multicast packets on a UDP port. These
packets contain one or more commands. This is useful to notify a whole
cluster of :program:`beng-proxy` servers of an event.

UDP is, by design, a lossy protocol. One must always consider that not
all nodes may have received a given packet.

The protocol does not implement authentication. The commands are
designed in a way that they do not affect security. However, it may pose
a weakness against DoS attacks, if an attacker manages to inject packets
into the internal network.

A command consists of a header containing length and command (network
byte order), and an optional payload. The payload is padded with null
bytes to the next 4 byte border.


.. _config.control:

Configuring
-----------

The configuration line option ``control`` allows you to specify an
address to listen on::

   control {
     bind "@beng-lb"
   }

This binds to the abstract address ``beng-lb``.

Known attributes:

- ``bind``: an adddress to bind to. May be the wildcard ``*`` or an
  IPv4/IPv6 address followed by a port. IPv6 addresses should be
  enclosed in square brackets to disambiguate the port
  separator. Local sockets start with a slash :file:`/`, and abstract
  sockets start with the symbol ``@``.

- ``multicast_group``: join this multicast group, which allows
  receiving multicast commands. Value is a multicast IPv4/IPv6
  address.  IPv6 addresses may contain a scope identifier after a
  percent sign (``%``).

- ``interface``: limit this listener to the given network interface.


Commands
--------

- ``NOP``: ignored

- ``TCACHE_INVALIDATE``: Invalidates translation cache entries. This
  packet follows the same semantics as the ``INVALIDATE`` translation
  response packet, but instead of passing just a list of command
  numbers referring to a real translation request, you need to send
  the values as well. The payload of this packet consists of one or
  more concatenated translation packets in network byte order, padded
  with zeroes to multiples of 4 bytes. If the payload is empty, then
  the whole translation cache will be flushed.

- ``ENABLE_NODE``: Re-enable the specified node after a failure,
  remove all failure/fade states. The payload is the node name
  according to :file:`lb.conf`, followed by a colon and the port
  number.

- ``FADE_NODE``: Fade out the specified node, preparing for its
  shutdown: the server will only be used for pre-existing sessions
  that refer to it. The payload is the node name according to
  :file:`lb.conf`, followed by a colon and the port number. The effect
  lasts for 3 hours.

.. _fade_children:

- ``FADE_CHILDREN``: Fade out child processes. If a payload is given,
  then this is a tag which fades only child processes with the given
  ``CHILD_TAG``.

- ``TERMINATE_CHILDREN``: Terminate all child processes with the
  ``CHILD_TAG`` from the payload.  Unlike ``FADE_CHILDREN``, this does
  not wait for completion of the child's currently work.Fade out child
  processes.

- ``DISABLE_URING``: Disable ``io_uring`` (temporarily).  Optional
  payload is a big-endian 32 bit integer containing the number of
  seconds after which it will be re-enabled automatically.  As this
  overrides any previous ``DISABLE_URING`` command, zero explicitly
  re-enables ``io_uring`` now.  This switch exists to make debugging
  with ``strace`` easier, because ``strace`` does not cope well with
  ``io_uring``.

- ``DISABLE_ZEROCONF``: Hide all registered Zeroconf services. To
  reveal them again, use ``ENABLE_ZEROCONF``. This is a privileged
  operation (only allowed through local socket by user root).

- ``ENABLE_ZEROCONF``: Re-publish all registered Zeroconf services to
  undo the effect of ``DISABLE_ZEROCONF``.

- ``FLUSH_NFS_CACHE``: Deprecated.

.. _flush_filter_cache:

- ``FLUSH_FILTER_CACHE``: Flush all items from the filter cache.  If a
  payload is given, then this is a tag which flushes only cache items
  with the given :ref:`CACHE_TAG <cache_tag>`.

.. _flush_http_cache:

- ``FLUSH_HTTP_CACHE``: Flush all items from the HTTP cache with the
  given :ref:`CACHE_TAG <cache_tag>`.

.. _discard_session:

- ``DISCARD_SESSION``: Discard the session with the given
  :ref:`ATTACH_SESSION <t_attach_session>` value.

- ``ENABLE_QUEUE`` / ``DISABLE_QUEUE``: Enable the queue named in the
  payload.  (This is used only by `Workshop
  <https://github.com/CM4all/workshop>`__.)

.. _reload_state:

- ``RELOAD_STATE``: Reload state from the :ref:`state directories
  <state>` and apply it to the current process.

- ``DISCONNECT_DATABASE``: Disconnect all database connections
  matching the payload.  This is usually received and handled by
  `myproxy <https://github.com/CM4all/myproxy>`__ processes and the
  payload is the account identifier.

- ``RESET_LIMITER``: Reset data structures bound to the specified
  account that keep track of resource usage limits.  This shall be
  sent after resource limits have been changed and applies only to
  data structures that cannot automatically apply these because they
  do not have enough context (e.g. token buckets).

Only ``TCACHE_INVALIDATE``, ``FLUSH_FILTER_CACHE`` are allowed when
received via IP. The other commands are only accepted from clients
connected on a local socket (aka Unix Domain Socket, ``AF_LOCAL``).


Client
------

The Debian package ``cm4all-beng-control`` contains a generic
non-interactive client which can send commands to
:program:`beng-proxy` or :program:`beng-lb`.

The following command sends ``NOP`` to an abstract socket::

   cm4all-beng-control --server=@bp-control nop

And here is a multicast on interface ``eth1``::

   cm4all-beng-control --server=ff02::dead%eth1 nop
