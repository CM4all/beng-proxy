// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PingMonitor.hxx"
#include "MonitorHandler.hxx"
#include "MonitorClass.hxx"
#include "event/net/PingClient.hxx"
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
		 const LbMonitorConfig &,
		 SocketAddress address,
		 LbMonitorHandler &handler,
		 CancellablePointer &cancel_ptr)
{
	auto *ping = new LbPingMonitor(event_loop, handler);
	ping->Start(address, cancel_ptr);
}

const LbMonitorClass ping_monitor_class = {
	ping_monitor_run,
};
