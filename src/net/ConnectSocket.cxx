/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "system/fd_util.h"
#include "stopwatch.hxx"
#include "event/SocketEvent.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"

#include <socket/util.h>

#ifdef ENABLE_STOPWATCH
#include <socket/address.h>
#endif

#include <stdexcept>

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

void
ConnectSocketHandler::OnSocketConnectTimeout()
{
    /* default implementation falls back to OnSocketConnectError() */
    OnSocketConnectError(std::make_exception_ptr(std::runtime_error("Timeout")));
}

class ConnectSocket final : Cancellable {
    struct pool &pool;
    UniqueSocketDescriptor fd;
    SocketEvent event;

#ifdef ENABLE_STOPWATCH
    Stopwatch &stopwatch;
#endif

    ConnectSocketHandler &handler;

public:
    ConnectSocket(EventLoop &event_loop, struct pool &_pool,
                  UniqueSocketDescriptor &&_fd, unsigned timeout,
#ifdef ENABLE_STOPWATCH
                  Stopwatch &_stopwatch,
#endif
                  ConnectSocketHandler &_handler,
                  CancellablePointer &cancel_ptr)
        :pool(_pool), fd(std::move(_fd)),
         event(event_loop, fd.Get(), EV_WRITE,
               BIND_THIS_METHOD(EventCallback)),
#ifdef ENABLE_STOPWATCH
         stopwatch(_stopwatch),
#endif
         handler(_handler) {
        pool_ref(&pool);

        cancel_ptr = *this;

        const struct timeval tv = {
            .tv_sec = time_t(timeout),
            .tv_usec = 0,
        };
        event.Add(tv);
    }

    void Delete() {
        DeleteUnrefPool(pool, this);
    }

private:
    void EventCallback(unsigned events);

    /* virtual methods from class Cancellable */
    void Cancel() override;
};


/*
 * async operation
 *
 */

void
ConnectSocket::Cancel()
{
    assert(fd.IsDefined());

    event.Delete();
    Delete();
}


/*
 * libevent callback
 *
 */

inline void
ConnectSocket::EventCallback(unsigned events)
{
    if (events & EV_TIMEOUT) {
        handler.OnSocketConnectTimeout();
        Delete();
        return;
    }

    int s_err = fd.GetError();
    if (s_err == 0) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(&stopwatch, "connect");
        stopwatch_dump(&stopwatch);
#endif

        handler.OnSocketConnectSuccess(std::move(fd));
    } else {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno(s_err)));
    }

    Delete();
}


/*
 * constructor
 *
 */

void
client_socket_new(EventLoop &event_loop, struct pool &pool,
                  int domain, int type, int protocol,
                  bool ip_transparent,
                  const SocketAddress bind_address,
                  const SocketAddress address,
                  unsigned timeout,
                  ConnectSocketHandler &handler,
                  CancellablePointer &cancel_ptr)
{
    assert(!address.IsNull());

    UniqueSocketDescriptor fd;
    if (!fd.CreateNonBlock(domain, type, protocol)) {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno("Failed to create socket")));
        return;
    }

    if ((domain == PF_INET || domain == PF_INET6) && type == SOCK_STREAM &&
        !socket_set_nodelay(fd.Get(), true)) {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno()));
        return;
    }

    if (ip_transparent) {
        int on = 1;
        if (setsockopt(fd.Get(), SOL_IP, IP_TRANSPARENT, &on, sizeof on) < 0) {
            handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno("Failed to set IP_TRANSPARENT")));
            return;
        }
    }

    if (!bind_address.IsNull() && bind_address.IsDefined() &&
        !fd.Bind(bind_address)) {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno()));
        return;
    }

#ifdef ENABLE_STOPWATCH
    Stopwatch *stopwatch =
        stopwatch_sockaddr_new(&pool, address.GetAddress(), address.GetSize(),
                               nullptr);
#endif

    if (fd.Connect(address)) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(stopwatch, "connect");
        stopwatch_dump(stopwatch);
#endif

        handler.OnSocketConnectSuccess(std::move(fd));
    } else if (errno == EINPROGRESS) {
        NewFromPool<ConnectSocket>(pool, event_loop, pool,
                                   std::move(fd), timeout,
#ifdef ENABLE_STOPWATCH
                                   *stopwatch,
#endif
                                   handler, cancel_ptr);
    } else {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno()));
    }
}
