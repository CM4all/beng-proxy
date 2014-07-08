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

struct listener_handler {
    void (*connected)(SocketDescriptor &&fd, SocketAddress address, void *ctx);
    void (*error)(Error &&error, void *ctx);
};

class ServerSocket {
    SocketDescriptor fd;
    struct event event;

    const struct listener_handler &handler;
    void *handler_ctx;

public:
    ServerSocket(const struct listener_handler &_handler, void *_handler_ctx)
        :handler(_handler), handler_ctx(_handler_ctx) {}

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

private:
    void Callback();
    static void Callback(int fd, short event, void *ctx);
};

#endif
