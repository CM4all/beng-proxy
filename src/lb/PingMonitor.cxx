/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "MonitorHandler.hxx"
#include "MonitorClass.hxx"
#include "net/Ping.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

class LbPingMonitor final : PingClientHandler, Cancellable {
	PingClient ping;

	LbMonitorHandler &handler;

public:
	explicit LbPingMonitor(EventLoop &event_loop,
			       LbMonitorHandler &_handler)
		:ping(event_loop, *this), handler(_handler) {}

	void Start(SocketAddress address, CancellablePointer &cancel_ptr) {
		cancel_ptr = *this;
		ping.Start(address);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		delete this;
	}

	/* virtual methods from class PingClientHandler */
	void PingResponse() noexcept override {
		handler.Success();
		delete this;
	}

	void PingTimeout() noexcept override {
		handler.Timeout();
		delete this;
	}

	void PingError(std::exception_ptr ep) noexcept override {
		handler.Error(ep);
		delete this;
	}
};

static void
ping_monitor_run(EventLoop &event_loop,
		 gcc_unused const LbMonitorConfig &config,
		 SocketAddress address,
		 LbMonitorHandler &handler,
		 CancellablePointer &cancel_ptr)
{
	auto *ping = new LbPingMonitor(event_loop, handler);
	ping->Start(address, cancel_ptr);
}

const LbMonitorClass ping_monitor_class = {
	.run = ping_monitor_run,
};
