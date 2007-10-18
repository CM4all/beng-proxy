/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "client-socket.h"
#include "socket-util.h"

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

struct client_socket {
    int fd, s_err;
    struct event event;
    client_socket_callback_t callback;
    void *callback_ctx;
};

static void
client_socket_timer_callback(int fd, short event, void *ctx)
{
    client_socket_t client_socket = ctx;

    (void)event;

    fd = client_socket->fd;
    client_socket->fd = -1;

    client_socket->callback(fd, client_socket->s_err,
                            client_socket->callback_ctx);
    
}

static void
client_socket_event_callback(int fd, short event, void *ctx)
{
    client_socket_t client_socket = ctx;
    int ret;
    int s_err = 0;
    socklen_t s_err_size = sizeof(s_err);

    (void)event;

    assert(client_socket->fd >= 0);

    fd = client_socket->fd;
    client_socket->fd = -1;

    if (event & EV_TIMEOUT) {
        close(fd);
        client_socket->callback(-1, EINTR, client_socket->callback_ctx);
        return;
    }

    ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&s_err, &s_err_size);
    if (ret < 0)
        s_err = errno;

    if (s_err != 0) {
        close(fd);
        client_socket->callback(-1, s_err, client_socket->callback_ctx);
        return;
    }

    client_socket->callback(fd, 0, client_socket->callback_ctx);

    pool_commit();
}

int
client_socket_new(pool_t pool,
                  const struct sockaddr *addr, socklen_t addrlen,
                  client_socket_callback_t callback, void *ctx,
                  client_socket_t *client_socket_r)
{
    client_socket_t client_socket;
    int ret;

    assert(addr != NULL);
    assert(addrlen > 0);
    assert(callback != NULL);
    assert(client_socket_r != NULL);

    client_socket = p_calloc(pool, sizeof(*client_socket));
    if (client_socket == NULL)
        return -1;

    client_socket->callback = callback;
    client_socket->callback_ctx = ctx;

    client_socket->fd = socket(PF_INET, SOCK_STREAM, 0);
    if (client_socket->fd < 0)
        return -1;

    ret = socket_enable_nonblock(client_socket->fd);
    if (ret < 0) {
        int save_errno = errno;
        close(client_socket->fd);
        errno = save_errno;
        return -1;
    }

    ret = socket_enable_nodelay(client_socket->fd);
    if (ret < 0) {
        int save_errno = errno;
        close(client_socket->fd);
        errno = save_errno;
        return -1;
    }

    ret = connect(client_socket->fd, addr, addrlen);
    if (ret == 0) {
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 0,
        };

        evtimer_set(&client_socket->event, client_socket_timer_callback,
                    client_socket);
        evtimer_add(&client_socket->event, &tv);
    } else if (errno == EINPROGRESS) {
        struct timeval tv = {
            .tv_sec = 30,
            .tv_usec = 0,
        };

        event_set(&client_socket->event, client_socket->fd,
                  EV_WRITE|EV_TIMEOUT, client_socket_event_callback,
                  client_socket);
        event_add(&client_socket->event, &tv);
    } else {
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 0,
        };

        client_socket->s_err = errno;

        close(client_socket->fd);
        client_socket->fd = -1;

        evtimer_set(&client_socket->event, client_socket_timer_callback,
                    client_socket);
        evtimer_add(&client_socket->event, &tv);
    }

    *client_socket_r = client_socket;
    return 0;
}

void
client_socket_free(client_socket_t *client_socket_r)
{
    client_socket_t client_socket = *client_socket_r;
    *client_socket_r = NULL;

    assert(client_socket != NULL);

    if (client_socket->fd >= 0) {
        event_del(&client_socket->event);
        close(client_socket->fd);
        client_socket->fd = -1;
    }
}
