// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SynMonitor.hxx"
#include "MonitorHandler.hxx"
#include "MonitorClass.hxx"
#include "MonitorConfig.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Cancellable.hxx"

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

		const auto timeout = config.timeout > Event::Duration{}
		? config.timeout
			  : std::chrono::seconds(30);

		connect.Connect(address, timeout);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		delete this;
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor) noexcept override {
		/* ignore the socket, we don't need it */

		handler.Success();
		delete this;
	}

	void OnSocketConnectTimeout() noexcept override {
		handler.Timeout();
		delete this;
	}

	void OnSocketConnectError(std::exception_ptr ep) noexcept override {
		handler.Error(ep);
		delete this;
	}
};

/*
 * lb_monitor_class
 *
 */

static void
syn_monitor_run(EventLoop &event_loop,
		const LbMonitorConfig &config,
		SocketAddress address,
		LbMonitorHandler &handler,
		CancellablePointer &cancel_ptr)
{
	auto *syn = new LbSynMonitor(event_loop, handler);
	syn->Start(config, address, cancel_ptr);
}

const LbMonitorClass syn_monitor_class = {
	syn_monitor_run,
};
