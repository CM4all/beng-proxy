/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "listener.h"

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <event.h>

struct listener {
    int fd;
    struct event event;
    listener_callback_t callback;
    void *callback_ctx;
};

static int
set_non_block(int fd)
{
    int ret;

    ret = fcntl(fd, F_GETFL, 0);
    if (ret < 0)
        return ret;

    return fcntl(fd, F_SETFL, ret | O_NONBLOCK);
}

static void
listener_event_callback(int fd, short event, void *ctx)
{
    listener_t listener = ctx;
    struct sockaddr_storage sa;
    socklen_t sa_len;
    int remote_fd, ret;

    (void)event;

    sa_len = sizeof(sa);
    remote_fd = accept(fd, (struct sockaddr*)&sa, &sa_len);
    if (remote_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            perror("accept() failed");
        return;
    }

    ret = set_non_block(remote_fd);
    if (ret < 0) {
        perror("fcntl(O_NONBLOCK) failed");
        close(remote_fd);
        return;
    }

    listener->callback(remote_fd,
                       (const struct sockaddr*)&sa, sa_len,
                       listener->callback_ctx);
}

int
listener_tcp_port_new(pool_t pool, int port,
                      listener_callback_t callback, void *ctx,
                      listener_t *listener_r)
{
    listener_t listener;
    int ret, param;
    struct sockaddr_in6 sa6;

    assert(port > 0);
    assert(callback != NULL);
    assert(listener_r != NULL);

    listener = p_calloc(pool, sizeof(*listener));
    if (listener == NULL)
        return -1;

    listener->fd = socket(PF_INET6, SOCK_STREAM, 0);
    if (listener->fd < 0)
        return -1;

    param = 1;
    ret = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, &param, sizeof(param));
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return -1;
    }

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_addr = in6addr_any;
    sa6.sin6_port = htons(port);
    ret = bind(listener->fd, (const struct sockaddr*)&sa6, sizeof(sa6));
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return -1;
    }

    ret = listen(listener->fd, 16);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return -1;
    }

    ret = set_non_block(listener->fd);
    if (ret < 0) {
        int save_errno = errno;
        close(listener->fd);
        errno = save_errno;
        return -1;
    }

    listener->callback = callback;
    listener->callback_ctx = ctx;

    event_set(&listener->event, listener->fd,
              EV_READ|EV_PERSIST, listener_event_callback, listener);
    event_add(&listener->event, NULL);

    *listener_r = listener;

    return 0;
}

void
listener_free(listener_t *listener_r)
{
    listener_t listener = *listener_r;
    *listener_r = NULL;

    assert(listener != NULL);
    assert(listener->fd >= 0);

    event_del(&listener->event);
    close(listener->fd);
}
