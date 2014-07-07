/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.hxx"
#include "fd_util.h"
#include "pool.h"
#include "util/Error.hxx"
#include "net/SocketDescriptor.hxx"

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

#include <event.h>

struct listener {
    const SocketDescriptor fd;
    struct event event;

    const struct listener_handler &handler;
    void *handler_ctx;

    listener(SocketDescriptor &&_fd,
             const struct listener_handler &_handler, void *_handler_ctx)
        :fd(std::move(_fd)), handler(_handler), handler_ctx(_handler_ctx) {}
};

static void
listener_event_callback(int fd, short event gcc_unused, void *ctx)
{
    struct listener *listener = (struct listener *)ctx;
    struct sockaddr_storage sa;
    size_t sa_len;
    int remote_fd;

    sa_len = sizeof(sa);
    remote_fd = accept_cloexec_nonblock(fd, (struct sockaddr*)&sa, &sa_len);
    if (remote_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            Error error;
            error.SetErrno("accept() failed");
            listener->handler.error(std::move(error), listener->handler_ctx);
        }

        return;
    }

    if (!socket_set_nodelay(remote_fd, true)) {
        Error error;
        error.SetErrno("setsockopt(TCP_NODELAY) failed");

        close(remote_fd);
        listener->handler.error(std::move(error), listener->handler_ctx);
        return;
    }

    listener->handler.connected(remote_fd,
                                (const struct sockaddr*)&sa, sa_len,
                                listener->handler_ctx);

    pool_commit();
}

static gcc_always_inline uint16_t
my_htons(uint16_t x)
{
#ifdef __ICC
#ifdef __LITTLE_ENDIAN
    /* icc seriously doesn't like the htons() macro */
    return (uint16_t)((x >> 8) | (x << 8));
#else
    return x;
#endif
#else
    return (uint16_t)htons((uint16_t)x);
#endif
}

struct listener *
listener_new(int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             const struct listener_handler *handler, void *ctx,
             Error &error)
{
    assert(address != nullptr);
    assert(address_length > 0);
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    if (address->sa_family == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address;
        if (sun->sun_path[0] != '\0')
            /* delete non-abstract socket files before reusing them */
            unlink(sun->sun_path);
    }

    SocketDescriptor fd;
    if (!fd.CreateListen(family, socktype, protocol,
                         address, address_length, error))
        return nullptr;

    auto listener = new struct listener(std::move(fd), *handler, ctx);

    event_set(&listener->event, listener->fd.Get(),
              EV_READ|EV_PERSIST, listener_event_callback, listener);

    listener_event_add(listener);

    return listener;
}

struct listener *
listener_tcp_port_new(int port,
                      const struct listener_handler *handler, void *ctx,
                      Error &error)
{
    struct listener *listener;
    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    assert(port > 0);
    assert(handler != nullptr);
    assert(handler->connected != nullptr);
    assert(handler->error != nullptr);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = my_htons((uint16_t)port);

    listener = listener_new(PF_INET6, SOCK_STREAM, 0,
                            (const struct sockaddr *)&sa6, sizeof(sa6),
                            handler, ctx, IgnoreError());
    if (listener != nullptr)
        return listener;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = my_htons((uint16_t)port);

    return listener_new(PF_INET, SOCK_STREAM, 0,
                        (const struct sockaddr *)&sa4, sizeof(sa4),
                        handler, ctx, error);
}

void
listener_free(struct listener **listener_r)
{
    struct listener *listener = *listener_r;
    *listener_r = nullptr;

    assert(listener != nullptr);
    assert(listener->fd.IsDefined());

    listener_event_del(listener);
    delete listener;
}

void
listener_event_add(struct listener *listener)
{
    event_add(&listener->event, nullptr);
}

void
listener_event_del(struct listener *listener)
{
    event_del(&listener->event);
}
