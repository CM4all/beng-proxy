/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"
#include "fd_util.h"
#include "pool.h"

#include <socket/util.h>
#include <socket/address.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include <event.h>

struct listener {
    int fd;
    struct event event;

    const struct listener_handler *handler;
    void *handler_ctx;
};

static void
listener_event_callback(int fd, short event gcc_unused, void *ctx)
{
    struct listener *listener = ctx;
    struct sockaddr_storage sa;
    size_t sa_len;
    int remote_fd;

    sa_len = sizeof(sa);
    remote_fd = accept_cloexec_nonblock(fd, (struct sockaddr*)&sa, &sa_len);
    if (remote_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            GError *error = g_error_new(g_file_error_quark(), errno,
                                        "accept() failed: %s",
                                        g_strerror(errno));
            listener->handler->error(error, listener->handler_ctx);
        }

        return;
    }

    if (!socket_set_nodelay(remote_fd, true)) {
        GError *error = g_error_new(g_file_error_quark(), errno,
                                    "setsockopt(TCP_NODELAY) failed: %s",
                                    g_strerror(errno));
        close(remote_fd);
        listener->handler->error(error, listener->handler_ctx);
        return;
    }

    listener->handler->connected(remote_fd,
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
listener_new(struct pool *pool, int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             const struct listener_handler *handler, void *ctx,
             GError **error_r)
{
    struct listener *listener;
    int ret, param;

    assert(address != NULL);
    assert(address_length > 0);
    assert(handler != NULL);
    assert(handler->connected != NULL);
    assert(handler->error != NULL);

    listener = p_calloc(pool, sizeof(*listener));
    listener->fd = socket_cloexec_nonblock(family, socktype, protocol);
    if (listener->fd < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "Failed to create socket: %s", strerror(errno));
        return NULL;
    }

    param = 1;
    ret = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
    if (ret < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "Failed to configure SO_REUSEADDR: %s", strerror(errno));
        close(listener->fd);
        return NULL;
    }

    if (address->sa_family == AF_UNIX) {
        const struct sockaddr_un *sun = (const struct sockaddr_un *)address;
        unlink(sun->sun_path);
    }

    ret = bind(listener->fd, address, address_length);
    if (ret < 0) {
        char buffer[64];
        socket_address_to_string(buffer, sizeof(buffer),
                                 address, address_length);
        g_set_error(error_r, g_file_error_quark(), errno,
                    "Failed to bind to '%s': %s", buffer, strerror(errno));
        close(listener->fd);
        return NULL;
    }

    ret = listen(listener->fd, 64);
    if (ret < 0) {
        g_set_error(error_r, g_file_error_quark(), errno,
                    "Failed to listen: %s", strerror(errno));
        close(listener->fd);
        return NULL;
    }

    listener->handler = handler;
    listener->handler_ctx = ctx;

    event_set(&listener->event, listener->fd,
              EV_READ|EV_PERSIST, listener_event_callback, listener);

    listener_event_add(listener);

    return listener;
}

struct listener *
listener_tcp_port_new(struct pool *pool, int port,
                      const struct listener_handler *handler, void *ctx,
                      GError **error_r)
{
    struct listener *listener;
    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    assert(port > 0);
    assert(handler != NULL);
    assert(handler->connected != NULL);
    assert(handler->error != NULL);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = my_htons((uint16_t)port);

    listener = listener_new(pool, PF_INET6, SOCK_STREAM, 0,
                            (const struct sockaddr *)&sa6, sizeof(sa6),
                            handler, ctx, NULL);
    if (listener != NULL)
        return listener;

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = my_htons((uint16_t)port);

    return listener_new(pool, PF_INET, SOCK_STREAM, 0,
                        (const struct sockaddr *)&sa4, sizeof(sa4),
                        handler, ctx, error_r);
}

void
listener_free(struct listener **listener_r)
{
    struct listener *listener = *listener_r;
    *listener_r = NULL;

    assert(listener != NULL);
    assert(listener->fd >= 0);

    listener_event_del(listener);
    close(listener->fd);
}

void
listener_event_add(struct listener *listener)
{
    event_add(&listener->event, NULL);
}

void
listener_event_del(struct listener *listener)
{
    event_del(&listener->event);
}
