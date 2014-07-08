/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ServerSocket.hxx"
#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "StaticSocketAddress.hxx"
#include "fd_util.h"
#include "pool.h"
#include "util/Error.hxx"

#include <socket/util.h>
#include <socket/address.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static void
listener_event_callback(gcc_unused int fd, short event gcc_unused, void *ctx)
{
    ServerSocket *listener = (ServerSocket *)ctx;

    StaticSocketAddress remote_address;
    Error error;
    auto remote_fd = listener->fd.Accept(remote_address, error);
    if (!remote_fd.IsDefined()) {
        if (!error.IsDomain(errno_domain) ||
            (error.GetCode() != EAGAIN && error.GetCode() != EWOULDBLOCK))
            listener->handler.error(std::move(error), listener->handler_ctx);

        return;
    }

    if (!socket_set_nodelay(remote_fd.Get(), true)) {
        error.SetErrno("setsockopt(TCP_NODELAY) failed");
        listener->handler.error(std::move(error), listener->handler_ctx);
        return;
    }

    listener->handler.connected(std::move(remote_fd), remote_address,
                                listener->handler_ctx);

    pool_commit();
}

ServerSocket *
listener_new(int family, int socktype, int protocol,
             SocketAddress address,
             const struct listener_handler *handler, void *ctx,
             Error &error)
{
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    if (address.GetFamily() == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)(const struct sockaddr *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    SocketDescriptor fd;
    if (!fd.CreateListen(family, socktype, protocol,
                         address, error))
        return nullptr;

    auto listener = new ServerSocket(std::move(fd), *handler, ctx);

    event_set(&listener->event, listener->fd.Get(),
              EV_READ|EV_PERSIST, listener_event_callback, listener);

    listener_event_add(listener);

    return listener;
}

ServerSocket *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error)
{
    ServerSocket *listener;
    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    assert(port > 0);
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons(port);

    listener = listener_new(PF_INET6, SOCK_STREAM, 0,
                            SocketAddress((const struct sockaddr *)&sa6, sizeof(sa6)),
                            handler, ctx, IgnoreError());
    if (listener != nullptr)
        return listener;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = htons(port);

    return listener_new(PF_INET, SOCK_STREAM, 0,
                        SocketAddress((const struct sockaddr *)&sa4,
                                      sizeof(sa4)),
                        handler, ctx, error);
}

ServerSocket::~ServerSocket()
{
    event_del(&event);
}

void
listener_event_add(ServerSocket *listener)
{
    event_add(&listener->event, nullptr);
}

void
listener_event_del(ServerSocket *listener)
{
    event_del(&listener->event);
}
