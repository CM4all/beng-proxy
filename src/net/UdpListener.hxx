/*
 * Listener on a UDP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef UDP_LISTENER_HXX
#define UDP_LISTENER_HXX

#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <stddef.h>

class SocketAddress;
class UdpHandler;

class UdpListener {
    UniqueSocketDescriptor fd;
    SocketEvent event;

    UdpHandler &handler;

public:
    UdpListener(EventLoop &event_loop, UniqueSocketDescriptor &&_fd,
                UdpHandler &_handler);
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
    void SetFd(UniqueSocketDescriptor &&_fd);

    /**
     * Send a reply datagram to a client.
     *
     * Throws std::runtime_error on error.
     */
    void Reply(SocketAddress address,
               const void *data, size_t data_length);

private:
    void EventCallback(unsigned events);
};

#endif
