// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ExpectMonitor.hxx"
#include "MonitorHandler.hxx"
#include "MonitorClass.hxx"
#include "MonitorConfig.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketError.hxx"
#include "event/net/ConnectSocket.hxx"
#include "event/SocketEvent.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/FineTimerEvent.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"

#include <unistd.h>
#include <string.h>

class ExpectMonitor final : ConnectSocketHandler, Cancellable {
	const LbMonitorConfig &config;

	ConnectSocket connect;

	SocketDescriptor fd = SocketDescriptor::Undefined();

	SocketEvent event;
	CoarseTimerEvent timeout_event;

	/**
	 * A timer which is used to delay the recv() call, just in case
	 * the server sends the response in more than one packet.
	 */
	FineTimerEvent delay_event;

	LbMonitorHandler &handler;

public:
	ExpectMonitor(EventLoop &event_loop,
		      const LbMonitorConfig &_config,
		      LbMonitorHandler &_handler) noexcept
		:config(_config),
		 connect(event_loop, *this),
		 event(event_loop, BIND_THIS_METHOD(EventCallback)),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
		 delay_event(event_loop, BIND_THIS_METHOD(DelayCallback)),
		 handler(_handler) {}

	ExpectMonitor(const ExpectMonitor &other) = delete;

	void Start(SocketAddress address, CancellablePointer &cancel_ptr) noexcept {
		cancel_ptr = *this;

		const Event::Duration zero{};
		const auto timeout = config.connect_timeout > zero
			? config.connect_timeout
			: (config.timeout > zero
			   ? config.timeout
			   : std::chrono::seconds(30));

		connect.Connect(address, timeout);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;

	void OnSocketConnectTimeout() noexcept override {
		handler.Timeout();
		delete this;
	}

	void OnSocketConnectError(std::exception_ptr ep) noexcept override {
		handler.Error(ep);
		delete this;
	}

private:
	void EventCallback(unsigned events) noexcept;
	void OnTimeout() noexcept;
	void DelayCallback() noexcept;
};

static bool
check_expectation(std::span<const std::byte> received,
		  const char *expect) noexcept
{
	return memmem(received.data(), received.size(), expect, strlen(expect)) != nullptr;
}

/*
 * async operation
 *
 */

void
ExpectMonitor::Cancel() noexcept
{
	if (fd.IsDefined()) {
		event.Cancel();
		timeout_event.Cancel();
		delay_event.Cancel();
		fd.Close();
	}

	delete this;
}

/*
 * libevent callback
 *
 */

inline void
ExpectMonitor::EventCallback(unsigned) noexcept
{
	event.Cancel();

	/* wait 10ms before we start reading */
	delay_event.Schedule(std::chrono::milliseconds(10));
}

inline void
ExpectMonitor::OnTimeout() noexcept
{
	fd.Close();
	handler.Timeout();

	delete this;
}

void
ExpectMonitor::DelayCallback() noexcept
{
	std::byte buffer[1024];

	ssize_t nbytes = fd.Receive(buffer, MSG_DONTWAIT);
	if (nbytes < 0) {
		auto e = MakeSocketError("Failed to receive");
		fd.Close();
		handler.Error(std::make_exception_ptr(e));
	} else if (!config.fade_expect.empty() &&
		   check_expectation(std::span{buffer}.first(nbytes),
				     config.fade_expect.c_str())) {
		fd.Close();
		handler.Fade();
	} else if (config.expect.empty() ||
		   check_expectation(std::span{buffer}.first(nbytes),
				     config.expect.c_str())) {
		fd.Close();
		handler.Success();
	} else {
		fd.Close();
		handler.Error(std::make_exception_ptr(std::runtime_error("Expectation failed")));
	}

	delete this;
}

/*
 * client_socket handler
 *
 */

void
ExpectMonitor::OnSocketConnectSuccess(UniqueSocketDescriptor new_fd) noexcept
{
	if (!config.send.empty()) {
		ssize_t nbytes = new_fd.Send(AsBytes(config.send), MSG_DONTWAIT);
		if (nbytes < 0) {
			handler.Error(std::make_exception_ptr(MakeSocketError("Failed to send")));
			delete this;
			return;
		}
	}

	const auto expect_timeout = config.timeout > Event::Duration{}
	? config.timeout
		  : std::chrono::seconds(10);

	fd = new_fd.Release();
	event.Open(fd);
	event.ScheduleRead();
	timeout_event.Schedule(expect_timeout);
}

/*
 * lb_monitor_class
 *
 */

static void
expect_monitor_run(EventLoop &event_loop,
		   const LbMonitorConfig &config,
		   SocketAddress address,
		   LbMonitorHandler &handler,
		   CancellablePointer &cancel_ptr)
{
	ExpectMonitor *expect = new ExpectMonitor(event_loop, config,
						  handler);

	expect->Start(address, cancel_ptr);
}

const LbMonitorClass expect_monitor_class = {
	expect_monitor_run,
};
