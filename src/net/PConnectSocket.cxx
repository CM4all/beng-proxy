/*
 * TCP client socket with asynchronous connect.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "PConnectSocket.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "system/fd_util.h"
#include "stopwatch.hxx"
#include "pool.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

class PConnectSocket final : Cancellable, ConnectSocketHandler {
    struct pool &pool;

    ConnectSocket connect;

#ifdef ENABLE_STOPWATCH
    Stopwatch &stopwatch;
#endif

    ConnectSocketHandler &handler;

public:
    PConnectSocket(EventLoop &event_loop, struct pool &_pool,
                   UniqueSocketDescriptor &&_fd, unsigned timeout,
#ifdef ENABLE_STOPWATCH
                   Stopwatch &_stopwatch,
#endif
                   ConnectSocketHandler &_handler,
                   CancellablePointer &cancel_ptr)
        :pool(_pool),
         connect(event_loop, *this),
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

        connect.WaitConnected(std::move(_fd), tv);
    }

    void Delete() {
        DeleteUnrefPool(pool, this);
    }

private:
    void EventCallback(unsigned events);

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) override;
    void OnSocketConnectTimeout() override;
    void OnSocketConnectError(std::exception_ptr ep) override;
};


/*
 * async operation
 *
 */

void
PConnectSocket::Cancel()
{
    assert(connect.IsPending());

    Delete();
}


/*
 * ConnectSocketHandler
 *
 */

void
PConnectSocket::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd)
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "connect");
    stopwatch_dump(&stopwatch);
#endif

    handler.OnSocketConnectSuccess(std::move(fd));
    Delete();
}

void
PConnectSocket::OnSocketConnectTimeout()
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "timeout");
    stopwatch_dump(&stopwatch);
#endif

    handler.OnSocketConnectTimeout();
    Delete();
}

void
PConnectSocket::OnSocketConnectError(std::exception_ptr ep)
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "error");
    stopwatch_dump(&stopwatch);
#endif

    handler.OnSocketConnectError(ep);
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
        !fd.SetNoDelay()) {
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
    Stopwatch *stopwatch = stopwatch_new(&pool, address, nullptr);
#endif

    if (fd.Connect(address)) {
#ifdef ENABLE_STOPWATCH
        stopwatch_event(stopwatch, "connect");
        stopwatch_dump(stopwatch);
#endif

        handler.OnSocketConnectSuccess(std::move(fd));
    } else if (errno == EINPROGRESS) {
        NewFromPool<PConnectSocket>(pool, event_loop, pool,
                                    std::move(fd), timeout,
#ifdef ENABLE_STOPWATCH
                                    *stopwatch,
#endif
                                    handler, cancel_ptr);
    } else {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno()));
    }
}
