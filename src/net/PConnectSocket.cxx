/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "PConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "stopwatch.hxx"
#include "pool/pool.hxx"
#include "system/Error.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

class PConnectSocket final : Cancellable, ConnectSocketHandler {
    ConnectSocket connect;

#ifdef ENABLE_STOPWATCH
    Stopwatch &stopwatch;
#endif

    ConnectSocketHandler &handler;

public:
    PConnectSocket(EventLoop &event_loop,
                   UniqueSocketDescriptor &&_fd, Event::Duration timeout,
#ifdef ENABLE_STOPWATCH
                   Stopwatch &_stopwatch,
#endif
                   ConnectSocketHandler &_handler,
                   CancellablePointer &cancel_ptr)
        :connect(event_loop, *this),
#ifdef ENABLE_STOPWATCH
         stopwatch(_stopwatch),
#endif
         handler(_handler) {
        cancel_ptr = *this;

        connect.WaitConnected(std::move(_fd), timeout);
    }

    void Delete() {
        this->~PConnectSocket();
    }

private:
    void EventCallback(unsigned events);

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
    void OnSocketConnectTimeout() noexcept override;
    void OnSocketConnectError(std::exception_ptr ep) noexcept override;
};


/*
 * async operation
 *
 */

void
PConnectSocket::Cancel() noexcept
{
    assert(connect.IsPending());

    Delete();
}


/*
 * ConnectSocketHandler
 *
 */

void
PConnectSocket::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "connect");
    stopwatch_dump(&stopwatch);
#endif

    auto &_handler = handler;
    Delete();

    _handler.OnSocketConnectSuccess(std::move(fd));
}

void
PConnectSocket::OnSocketConnectTimeout() noexcept
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "timeout");
    stopwatch_dump(&stopwatch);
#endif

    auto &_handler = handler;
    Delete();

    _handler.OnSocketConnectTimeout();
}

void
PConnectSocket::OnSocketConnectError(std::exception_ptr ep) noexcept
{
#ifdef ENABLE_STOPWATCH
    stopwatch_event(&stopwatch, "error");
    stopwatch_dump(&stopwatch);
#endif

    auto &_handler = handler;
    Delete();

    _handler.OnSocketConnectError(ep);
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
                  Event::Duration timeout,
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
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno("Failed to set TCP_NODELAY")));
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
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno("Failed to bind socket")));
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
        NewFromPool<PConnectSocket>(pool, event_loop,
                                    std::move(fd), timeout,
#ifdef ENABLE_STOPWATCH
                                    *stopwatch,
#endif
                                    handler, cancel_ptr);
    } else {
        handler.OnSocketConnectError(std::make_exception_ptr(MakeErrno("Failed to connect")));
    }
}
