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
#include "event/Duration.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

#include <unistd.h>
#include <sys/socket.h>

class LbSynMonitor final : ConnectSocketHandler, Cancellable {
    ConnectSocket connect;

    LbMonitorHandler &handler;

public:
    LbSynMonitor(EventLoop &event_loop,
                 LbMonitorHandler &_handler)
        :connect(event_loop, *this),
         handler(_handler) {}

    void Start(const LbMonitorConfig &config, SocketAddress address,
               CancellablePointer &cancel_ptr) {
        cancel_ptr = *this;

        const unsigned timeout = config.timeout > 0
            ? config.timeout
            : 30;

        connect.Connect(address,
                        ToEventDuration(std::chrono::seconds(timeout)));
    }

private:
    /* virtual methods from class Cancellable */
    void Cancel() override {
        delete this;
    }

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(UniqueSocketDescriptor &&) override {
        /* ignore the socket, we don't need it */

        handler.Success();
        delete this;
    }

    void OnSocketConnectTimeout() override {
        handler.Timeout();
        delete this;
    }

    void OnSocketConnectError(std::exception_ptr ep) override {
        handler.Error(ep);
        delete this;
    }
};

/*
 * lb_monitor_class
 *
 */

static void
syn_monitor_run(EventLoop &event_loop, struct pool &,
                const LbMonitorConfig &config,
                SocketAddress address,
                LbMonitorHandler &handler,
                CancellablePointer &cancel_ptr)
{
    auto *syn = new LbSynMonitor(event_loop, handler);
    syn->Start(config, address, cancel_ptr);
}

const LbMonitorClass syn_monitor_class = {
    .run = syn_monitor_run,
};
