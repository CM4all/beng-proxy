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

#include "SynMonitor.hxx"
#include "Monitor.hxx"
#include "MonitorConfig.hxx"
#include "pool.hxx"
#include "net/PConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

#include <unistd.h>
#include <sys/socket.h>

class LbSynMonitor final : public ConnectSocketHandler {
    LbMonitorHandler &handler;

public:
    explicit LbSynMonitor(LbMonitorHandler &_handler):handler(_handler) {}

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&) override {
        /* ignore the socket, we don't need it */

        handler.Success();
    }

    void OnSocketConnectTimeout() override {
        handler.Timeout();
    }

    void OnSocketConnectError(std::exception_ptr ep) override {
        handler.Error(ep);
    }
};

/*
 * lb_monitor_class
 *
 */

static void
syn_monitor_run(EventLoop &event_loop, struct pool &pool,
                const LbMonitorConfig &config,
                SocketAddress address,
                LbMonitorHandler &handler,
                CancellablePointer &cancel_ptr)
{
    const unsigned timeout = config.timeout > 0
        ? config.timeout
        : 30;

    auto *syn = NewFromPool<LbSynMonitor>(pool, handler);
    client_socket_new(event_loop, pool, address.GetFamily(), SOCK_STREAM, 0,
                      false,
                      SocketAddress::Null(),
                      address,
                      timeout,
                      *syn,
                      cancel_ptr);
}

const LbMonitorClass syn_monitor_class = {
    .run = syn_monitor_run,
};
