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

#include "PingMonitor.hxx"
#include "Monitor.hxx"
#include "pool.hxx"
#include "net/Ping.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

class LbPingClientHandler final : public PingClientHandler {
    LbMonitorHandler &handler;

public:
    explicit LbPingClientHandler(LbMonitorHandler &_handler)
        :handler(_handler) {}

    void PingResponse() override {
        handler.Success();
    }

    void PingTimeout() override {
        handler.Timeout();
    }

    void PingError(std::exception_ptr ep) override {
        handler.Error(ep);
    }
};

static void
ping_monitor_run(EventLoop &event_loop, struct pool &pool,
                 gcc_unused const LbMonitorConfig &config,
                 SocketAddress address,
                 LbMonitorHandler &handler,
                 CancellablePointer &cancel_ptr)
{
    ping(event_loop, pool, address,
         *NewFromPool<LbPingClientHandler>(pool, handler),
         cancel_ptr);
}

const LbMonitorClass ping_monitor_class = {
    .run = ping_monitor_run,
};
