/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"
#include "socket-util.h"
#include "fd-util.h"

#include <daemon/log.h>
#include <socket/util.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
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
    listener_callback_t callback;
    void *callback_ctx;
};

static void
listener_event_callback(int fd, short event __attr_unused, void *ctx)
{
    struct listener *listener = ctx;
    struct sockaddr_storage sa;
    socklen_t sa_len;
    int remote_fd, ret;

    sa_len = sizeof(sa);
    remote_fd = accept(fd, (struct sockaddr*)&sa, &sa_len);
    if (remote_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            daemon_log(1, "accept() failed: %s\n", strerror(errno));
        return;
    }

    ret = fd_set_cloexec(remote_fd);
    if (ret < 0) {
        daemon_log(1, "fd_set_cloexec() failed: %s\n", strerror(errno));
        close(remote_fd);
        return;
    }

    ret = socket_set_nonblock(remote_fd, 1);
    if (ret < 0) {
        daemon_log(1, "fcntl(O_NONBLOCK) failed: %s\n", strerror(errno));
        close(remote_fd);
        return;
    }

    if (!socket_set_nodelay(remote_fd, true)) {
        daemon_log(1, "setsockopt(TCP_NODELAY) failed: %s\n", strerror(errno));
        close(remote_fd);
        return;
    }

    listener->callback(remote_fd,
                       (const struct sockaddr*)&sa, sa_len,
                       listener->callback_ctx);

    pool_commit();
}

static __attr_always_inline uint16_t
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
listener_new(pool_t pool, int family, int socktype, int protocol,
             const struct sockaddr *address, size_t address_length,
             listener_callback_t callback, void *ctx)
{
    struct listener *listener;
    int ret, param;

    assert(address != NULL);
    assert(address_length > 0);
    assert(callback != NULL);

    listener = p_calloc(pool, sizeof(*listener));
    listener->fd = socket(family, socktype, protocol);
    if (listener->fd < 0)
        return NULL;

    param = 1;
    ret = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return NULL;
    }

    ret = bind(listener->fd, address, address_length);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return NULL;
    }

    ret = listen(listener->fd, 16);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return NULL;
    }

    ret = socket_set_nonblock(listener->fd, 1);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return NULL;
    }

    ret = fd_set_cloexec(listener->fd);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return NULL;
    }

    listener->callback = callback;
    listener->callback_ctx = ctx;

    listener_event_add(listener);

    return listener;
}

int
listener_tcp_port_new(pool_t pool, int port,
                      listener_callback_t callback, void *ctx,
                      struct listener **listener_r)
{
    struct listener *listener;
    struct sockaddr_in6 sa6;
    struct sockaddr_in sa4;

    assert(port > 0);
    assert(callback != NULL);
    assert(listener_r != NULL);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = my_htons((uint16_t)port);

    listener = listener_new(pool, PF_INET6, SOCK_STREAM, 0,
                            (const struct sockaddr *)&sa6, sizeof(sa6),
                            callback, ctx);
    if (listener != NULL) {
        *listener_r = listener;
        return 0;
    }

    memset(&sa4, 0, sizeof(sa4));
    sa4.sin_family = AF_INET;
    sa4.sin_addr.s_addr = INADDR_ANY;
    sa4.sin_port = my_htons((uint16_t)port);

    listener = listener_new(pool, PF_INET, SOCK_STREAM, 0,
                            (const struct sockaddr *)&sa4, sizeof(sa4),
                            callback, ctx);
    if (listener != NULL) {
        *listener_r = listener;
        return 0;
    }

    return -1;
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
    event_set(&listener->event, listener->fd,
              EV_READ|EV_PERSIST, listener_event_callback, listener);
    event_add(&listener->event, NULL);
}

void
listener_event_del(struct listener *listener)
{
    event_del(&listener->event);
}
