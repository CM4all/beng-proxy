/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SERVER_SOCKET_HXX
#define SERVER_SOCKET_HXX

#include "SocketDescriptor.hxx"

#include <event.h>

#include <utility>

class SocketAddress;
class Error;

class ServerSocket {
    SocketDescriptor fd;
    struct event event;

public:
    ~ServerSocket();

    bool Listen(int family, int socktype, int protocol,
                SocketAddress address, Error &error);

    bool ListenTCP(unsigned port, Error &error);

    void AddEvent() {
        event_add(&event, nullptr);
    }

    void RemoveEvent() {
        event_del(&event);
    }

protected:
    /**
     * A new incoming connection has been established.
     *
     * @param fd the socket owned by the callee
     */
    virtual void OnAccept(SocketDescriptor &&fd, SocketAddress address) = 0;
    virtual void OnAcceptError(Error &&error) = 0;

private:
    void Callback();
    static void Callback(int fd, short event, void *ctx);
};

#endif
