/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UDP_LISTENER_HXX
#define BENG_PROXY_UDP_LISTENER_HXX

#include <stddef.h>

struct in_addr;
class SocketAddress;
class UdpListener;
class Error;

class UdpHandler {
public:
    /**
     * @param uid the peer process uid, or -1 if unknown
     */
    virtual void OnUdpDatagram(const void *data, size_t length,
                               SocketAddress address, int uid) = 0;

    virtual void OnUdpError(Error &&error) = 0;
};

UdpListener *
udp_listener_new(SocketAddress address,
                 UdpHandler &handler);

UdpListener *
udp_listener_port_new(const char *host_and_port, int default_port,
                      UdpHandler &handler);

void
udp_listener_free(UdpListener *udp);

/**
 * Enable the object after it has been disabled by
 * udp_listener_disable().  A new object is enabled by default.
 */
void
udp_listener_enable(UdpListener *udp);

/**
 * Disable the object temporarily.  To undo this, call
 * udp_listener_enable().
 */
void
udp_listener_disable(UdpListener *udp);

/**
 * Replaces the socket.  The old one is closed, and the new one is now
 * owned by this object.
 *
 * This may only be called on an object that is "enabled", see
 * udp_listener_enable().
 */
void
udp_listener_set_fd(UdpListener *udp, int fd);

/**
 * Joins the specified multicast group.
 */
void
udp_listener_join4(UdpListener *udp, const struct in_addr *group);

/**
 * Send a reply datagram to a client.
 */
bool
udp_listener_reply(UdpListener *udp, SocketAddress address,
                   const void *data, size_t data_length,
                   Error &error_r);

#endif
