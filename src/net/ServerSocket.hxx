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

class SocketDescriptor;
class SocketAddress;
class Error;

struct listener_handler {
    void (*connected)(SocketDescriptor &&fd, SocketAddress address, void *ctx);
    void (*error)(Error &&error, void *ctx);
};

class ServerSocket {
    const SocketDescriptor fd;
    struct event event;

    const struct listener_handler &handler;
    void *handler_ctx;

public:
    ServerSocket(SocketDescriptor &&_fd,
                 const struct listener_handler &_handler, void *_handler_ctx)
        :fd(std::move(_fd)), handler(_handler), handler_ctx(_handler_ctx) {
        event_set(&event, fd.Get(), EV_READ|EV_PERSIST, Callback, this);
        AddEvent();
    }

    ~ServerSocket();

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

ServerSocket *
listener_new(int family, int socktype, int protocol,
             SocketAddress address,
             const struct listener_handler *handler, void *ctx,
             Error &error);

ServerSocket *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error);

#endif
