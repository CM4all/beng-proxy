/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConnectSocket.hxx"
#include "async.h"
#include "fd_util.h"
#include "stopwatch.h"
#include "pevent.h"
#include "gerrno.h"
#include "util/Cast.hxx"

#include <socket/util.h>

#ifdef ENABLE_STOPWATCH
#include <socket/address.h>
#endif

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <event.h>

#ifndef IP_TRANSPARENT
/* necessary on Debian Squeeze */
#define IP_TRANSPARENT 19
#endif

struct ConnectSocket {
    struct async_operation operation;
    struct pool *const pool;
    int fd;
    struct event event;

    const ConnectSocketHandler *const handler;
    void *const handler_ctx;

#ifdef ENABLE_STOPWATCH
    struct stopwatch *stopwatch;
#endif

    ConnectSocket(struct pool &_pool, int _fd,
                  const ConnectSocketHandler &_handler, void *ctx)
        :pool(&_pool), fd(_fd), handler(&_handler), handler_ctx(ctx) {}
};


/*
 * async operation
 *
 */

static ConnectSocket *
async_to_client_socket(struct async_operation *ao)
{
    return ContainerCast(ao, ConnectSocket, operation);
}

static void
client_socket_abort(struct async_operation *ao)
{
    ConnectSocket *client_socket = async_to_client_socket(ao);

    assert(client_socket != nullptr);
    assert(client_socket->fd >= 0);

    p_event_del(&client_socket->event, client_socket->pool);
    close(client_socket->fd);
    pool_unref(client_socket->pool);
}

static const struct async_operation_class client_socket_operation = {
    .abort = client_socket_abort,
};


/*
 * libevent callback
 *
 */

static void
client_socket_event_callback(int fd, short event gcc_unused, void *ctx)
{
    ConnectSocket *client_socket = (ConnectSocket *)ctx;
    int ret;
    int s_err = 0;
    socklen_t s_err_size = sizeof(s_err);

    assert(client_socket->fd == fd);

    p_event_consumed(&client_socket->event, client_socket->pool);

    async_operation_finished(&client_socket->operation);

    if (event & EV_TIMEOUT) {
        close(fd);
        client_socket->handler->timeout(client_socket->handler_ctx);
        pool_unref(client_socket->pool);
        pool_commit();
        return;
    }

    ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&s_err, &s_err_size);
    if (ret < 0)
        s_err = errno;

    if (s_err == 0) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(client_socket->stopwatch, "connect");
        stopwatch_dump(client_socket->stopwatch);
#endif

        client_socket->handler->success(fd, client_socket->handler_ctx);
    } else {
        close(fd);

        GError *error = new_error_errno2(s_err);
        client_socket->handler->error(error, client_socket->handler_ctx);
    }

    pool_unref(client_socket->pool);
    pool_commit();
}


/*
 * constructor
 *
 */

void
client_socket_new(struct pool &pool,
                  int domain, int type, int protocol,
                  bool ip_transparent,
                  const struct sockaddr *bind_addr, size_t bind_addrlen,
                  const struct sockaddr *addr, size_t addrlen,
                  unsigned timeout,
                  const ConnectSocketHandler &handler, void *ctx,
                  struct async_operation_ref &async_ref)
{
    int fd, ret;
#ifdef ENABLE_STOPWATCH
    struct stopwatch *stopwatch;
#endif

    assert(addr != nullptr);
    assert(addrlen > 0);

    fd = socket_cloexec_nonblock(domain, type, protocol);
    if (fd < 0) {
        GError *error = new_error_errno();
        handler.error(error, ctx);
        return;
    }

    if ((domain == PF_INET || domain == PF_INET6) && type == SOCK_STREAM &&
        !socket_set_nodelay(fd, true)) {
        GError *error = new_error_errno();
        close(fd);
        handler.error(error, ctx);
        return;
    }

    if (ip_transparent) {
        int on = 1;
        if (setsockopt(fd, SOL_IP, IP_TRANSPARENT, &on, sizeof on) < 0) {
            GError *error = new_error_errno_msg("Failed to set IP_TRANSPARENT");
            close(fd);
            handler.error(error, ctx);
            return;
        }
    }

    if (bind_addr != nullptr && bind(fd, bind_addr, bind_addrlen) < 0) {
        GError *error = new_error_errno();
        close(fd);
        handler.error(error, ctx);
        return;
    }

#ifdef ENABLE_STOPWATCH
    stopwatch = stopwatch_sockaddr_new(&pool, addr, addrlen, nullptr);
#endif

    ret = connect(fd, addr, addrlen);
    if (ret == 0) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(stopwatch, "connect");
        stopwatch_dump(stopwatch);
#endif

        handler.success(fd, ctx);
    } else if (errno == EINPROGRESS) {
        const struct timeval tv = {
            .tv_sec = timeout,
            .tv_usec = 0,
        };

        pool_ref(&pool);
        auto client_socket =
            NewFromPool<ConnectSocket>(&pool, pool, fd,
                                       handler, ctx);

#ifdef ENABLE_STOPWATCH
        client_socket->stopwatch = stopwatch;
#endif

        async_init(&client_socket->operation, &client_socket_operation);
        async_ref_set(&async_ref, &client_socket->operation);

        event_set(&client_socket->event, client_socket->fd,
                  EV_WRITE|EV_TIMEOUT, client_socket_event_callback,
                  client_socket);
        p_event_add(&client_socket->event, &tv,
                    client_socket->pool, "client_socket_event");
    } else {
        GError *error = new_error_errno();
        close(fd);
        handler.error(error, ctx);
    }
}
