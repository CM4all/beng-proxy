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
  according to lb.conf, followed by a colon and the port number.

- ``FADE_NODE``: Fade out the specified node, preparing for its
  shutdown: the server will only be used for pre-existing sessions
  that refer to it. The payload is the node name according to
  ``lb.conf``, followed by a colon and the port number. The effect
  lasts for 3 hours.

.. _fade_children:

- ``FADE_CHILDREN``: Fade out child processes. If a payload is given,
  then this is a tag which fades only child processes with the given
  ``CHILD_TAG``.

- ``DISABLE_ZEROCONF``: Hide all registered Zeroconf services. To
  reveal them again, use ``ENABLE_ZEROCONF``. This is a privileged
  operation (only allowed through local socket by user root).

- ``ENABLE_ZEROCONF``: Re-publish all registered Zeroconf services to
  undo the effect of ``DISABLE_ZEROCONF``.

- ``FLUSH_NFS_CACHE``: Flush all items from the NFS cache.

.. _flush_filter_cache:

- ``FLUSH_FILTER_CACHE``: Flush all items from the filter cache.  If a
  payload is given, then this is a tag which flushes only cache items
  with the given :ref:`CACHE_TAG <cache_tag>`.

Only ``TCACHE_INVALIDATE``, ``FLUSH_NFS_CACHE``,
``FLUSH_FILTER_CACHE``, ``STATS`` and ``NODE_STATUS`` are allowed when
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
