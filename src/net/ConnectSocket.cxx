/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConnectSocket.hxx"
#include "SocketDescriptor.hxx"
#include "SocketAddress.hxx"
#include "async.hxx"
#include "fd_util.h"
#include "stopwatch.h"
#include "pevent.hxx"
#include "gerrno.h"
#include "util/Cast.hxx"
#include "pool.hxx"

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

class ConnectSocket {
    struct async_operation operation;
    struct pool &pool;
    SocketDescriptor fd;
    struct event event;

#ifdef ENABLE_STOPWATCH
    struct stopwatch &stopwatch;
#endif

    const ConnectSocketHandler &handler;
    void *const handler_ctx;

public:
    ConnectSocket(struct pool &_pool, SocketDescriptor &&_fd, unsigned timeout,
#ifdef ENABLE_STOPWATCH
                  struct stopwatch &_stopwatch,
#endif
                  const ConnectSocketHandler &_handler, void *ctx,
                  struct async_operation_ref &async_ref)
        :pool(_pool), fd(std::move(_fd)),
#ifdef ENABLE_STOPWATCH
         stopwatch(_stopwatch),
#endif
         handler(_handler), handler_ctx(ctx) {
        pool_ref(&pool);

        operation.Init2<ConnectSocket, &ConnectSocket::operation,
                        &ConnectSocket::Abort>();
        async_ref.Set(operation);

        event_set(&event, fd.Get(),
                  EV_WRITE|EV_TIMEOUT, OnEvent,
                  this);

        const struct timeval tv = {
            .tv_sec = time_t(timeout),
            .tv_usec = 0,
        };
        p_event_add(&event, &tv, &pool, "client_socket_event");
    }

    void Delete() {
        DeleteUnrefPool(pool, this);
    }

private:
    void OnEvent(int _fd, short events);
    static void OnEvent(int fd, short event, void *ctx);

    void Abort();
};


/*
 * async operation
 *
 */

inline void
ConnectSocket::Abort()
{
    assert(fd.IsDefined());

    p_event_del(&event, &pool);
    Delete();
}


/*
 * libevent callback
 *
 */

inline void
ConnectSocket::OnEvent(gcc_unused int _fd, short events)
{
    assert(_fd == fd.Get());

    p_event_consumed(&event, &pool);

    operation.Finished();

    if (events & EV_TIMEOUT) {
        handler.timeout(handler_ctx);
        Delete();
        return;
    }

    int s_err = fd.GetError();
    if (s_err == 0) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(&stopwatch, "connect");
        stopwatch_dump(&stopwatch);
#endif

        handler.success(std::move(fd), handler_ctx);
    } else {
        GError *error = new_error_errno2(s_err);
        handler.error(error, handler_ctx);
    }

    Delete();
}

void
ConnectSocket::OnEvent(int fd, short event, void *ctx)
{
    ConnectSocket &client_socket = *(ConnectSocket *)ctx;

    client_socket.OnEvent(fd, event);
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
                  const SocketAddress bind_address,
                  const SocketAddress address,
                  unsigned timeout,
                  const ConnectSocketHandler &handler, void *ctx,
                  struct async_operation_ref &async_ref)
{
    assert(!address.IsNull());

    SocketDescriptor fd;
    if (!fd.Create(domain, type, protocol)) {
        GError *error = new_error_errno();
        handler.error(error, ctx);
        return;
    }

    if ((domain == PF_INET || domain == PF_INET6) && type == SOCK_STREAM &&
        !socket_set_nodelay(fd.Get(), true)) {
        GError *error = new_error_errno();
        handler.error(error, ctx);
        return;
    }

    if (ip_transparent) {
        int on = 1;
        if (setsockopt(fd.Get(), SOL_IP, IP_TRANSPARENT, &on, sizeof on) < 0) {
            GError *error = new_error_errno_msg("Failed to set IP_TRANSPARENT");
            handler.error(error, ctx);
            return;
        }
    }

    if (!bind_address.IsNull() && bind_address.IsDefined() &&
        !fd.Bind(bind_address)) {
        GError *error = new_error_errno();
        handler.error(error, ctx);
        return;
    }

#ifdef ENABLE_STOPWATCH
    struct stopwatch *stopwatch =
        stopwatch_sockaddr_new(&pool, address.GetAddress(), address.GetSize(),
                               nullptr);
#endif

    if (fd.Connect(address)) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(stopwatch, "connect");
        stopwatch_dump(stopwatch);
#endif

        handler.success(std::move(fd), ctx);
    } else if (errno == EINPROGRESS) {
        NewFromPool<ConnectSocket>(pool, pool, std::move(fd), timeout,
#ifdef ENABLE_STOPWATCH
                                   *stopwatch,
#endif
                                   handler, ctx, async_ref);
    } else {
        GError *error = new_error_errno();
        handler.error(error, ctx);
    }
}
