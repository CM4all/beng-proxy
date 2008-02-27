/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "client-socket.h"
#include "socket-util.h"
#include "async.h"

#include <inline/poison.h>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include <event.h>

struct client_socket {
    struct async_operation operation;
    int fd;
    struct event event;
    client_socket_callback_t callback;
    void *callback_ctx;
};


/*
 * async operation
 *
 */

static struct client_socket *
async_to_client_socket(struct async_operation *ao)
{
    return (struct client_socket*)(((char*)ao) - offsetof(struct client_socket, operation));
}

static void
client_socket_abort(struct async_operation *ao)
{
    struct client_socket *client_socket = async_to_client_socket(ao);

    assert(client_socket != NULL);
    assert(client_socket->fd >= 0);

    event_del(&client_socket->event);
    close(client_socket->fd);
}

static struct async_operation_class client_socket_operation = {
    .abort = client_socket_abort,
};


/*
 * libevent callback
 *
 */

static void
client_socket_event_callback(int fd, short event __attr_unused, void *ctx)
{
    struct client_socket *client_socket = ctx;
    int ret;
    int s_err = 0;
    socklen_t s_err_size = sizeof(s_err);

    assert(client_socket->fd == fd);

    async_poison(&client_socket->operation);

    if (event & EV_TIMEOUT) {
        close(fd);
        client_socket->callback(-1, EINTR, client_socket->callback_ctx);
        return;
    }

    ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&s_err, &s_err_size);
    if (ret < 0)
        s_err = errno;

    if (s_err == 0) {
        client_socket->callback(fd, 0, client_socket->callback_ctx);
    } else {
        close(fd);
        client_socket->callback(-1, s_err, client_socket->callback_ctx);
    }

    poison_noaccess(client_socket, sizeof(*client_socket));

    pool_commit();
}


/*
 * constructor
 *
 */

void
client_socket_new(pool_t pool,
                  int domain, int type, int protocol,
                  const struct sockaddr *addr, socklen_t addrlen,
                  client_socket_callback_t callback, void *ctx,
                  struct async_operation_ref *async_ref)
{
    int fd, ret;

    assert(addr != NULL);
    assert(addrlen > 0);
    assert(callback != NULL);

    fd = socket(domain, type, protocol);
    if (fd < 0) {
        callback(-1, errno, ctx);
        return;
    }

    ret = socket_set_nonblock(fd, 1);
    if (ret < 0) {
        int save_errno = errno;
        close(fd);
        callback(-1, save_errno, ctx);
        return;
    }

    ret = socket_set_nodelay(fd, 1);
    if (ret < 0) {
        int save_errno = errno;
        close(fd);
        callback(-1, save_errno, ctx);
        return;
    }

    ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        callback(fd, 0, ctx);
    } else if (errno == EINPROGRESS) {
        struct client_socket *client_socket;
        struct timeval tv = {
            .tv_sec = 30,
            .tv_usec = 0,
        };

        client_socket = p_malloc(pool, sizeof(*client_socket));
        client_socket->fd = fd;
        client_socket->callback = callback;
        client_socket->callback_ctx = ctx;

        async_init(&client_socket->operation, &client_socket_operation);
        async_ref_set(async_ref, &client_socket->operation);

        event_set(&client_socket->event, client_socket->fd,
                  EV_WRITE|EV_TIMEOUT, client_socket_event_callback,
                  client_socket);
        event_add(&client_socket->event, &tv);
    } else {
        int save_errno = errno;
        close(fd);
        callback(-1, save_errno, ctx);
    }
}
