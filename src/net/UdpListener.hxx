/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_LISTENER_HXX
#define UDP_LISTENER_HXX

#include "event/SocketEvent.hxx"

#include <stddef.h>

struct in_addr;
class SocketAddress;
class UdpHandler;

class UdpListener {
    int fd;
    SocketEvent event;

    UdpHandler &handler;

public:
    UdpListener(EventLoop &event_loop, int _fd, UdpHandler &_handler);
    ~UdpListener();

    /**
     * Enable the object after it has been disabled by Disable().  A
     * new object is enabled by default.
     */
    void Enable() {
        event.Add();
    }

    /**
     * Disable the object temporarily.  To undo this, call Enable().
     */
    void Disable() {
        event.Delete();
    }

    /**
     * Replaces the socket.  The old one is closed, and the new one is now
     * owned by this object.
     *
     * This may only be called on an object that is "enabled", see
     * Enable().
     */
    void SetFd(int _fd);

    /**
     * Joins the specified multicast group.
     */
    void Join4(const struct in_addr *group);

    /**
     * Send a reply datagram to a client.
     *
     * Throws std::runtime_error on error.
     */
    void Reply(SocketAddress address,
               const void *data, size_t data_length);

private:
    void EventCallback(short events);
};

UdpListener *
udp_listener_new(EventLoop &event_loop,
                 SocketAddress address,
                 UdpHandler &handler);

UdpListener *
udp_listener_port_new(EventLoop &event_loop,
                      const char *host_and_port, int default_port,
                      UdpHandler &handler);

#endif
