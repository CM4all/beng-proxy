// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PConnectSocket.hxx"
#include "ConnectSocketX.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>

class PConnectSocket final : Cancellable, ConnectSocketHandler {
	ConnectSocket connect;

	const StopwatchPtr stopwatch;

	ConnectSocketHandler &handler;

public:
	PConnectSocket(EventLoop &event_loop,
		       UniqueSocketDescriptor &&_fd, Event::Duration timeout,
		       StopwatchPtr &&_stopwatch,
		       ConnectSocketHandler &_handler,
		       CancellablePointer &cancel_ptr)
		:connect(event_loop, *this),
		 stopwatch(std::move(_stopwatch)),
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
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
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
PConnectSocket::OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept
{
	stopwatch.RecordEvent("connect");

	auto &_handler = handler;
	Delete();

	_handler.OnSocketConnectSuccess(std::move(fd));
}

void
PConnectSocket::OnSocketConnectTimeout() noexcept
{
	stopwatch.RecordEvent("timeout");

	auto &_handler = handler;
	Delete();

	_handler.OnSocketConnectTimeout();
}

void
PConnectSocket::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("error");

	auto &_handler = handler;
	Delete();

	_handler.OnSocketConnectError(ep);
}

/*
 * constructor
 *
 */

void
client_socket_new(EventLoop &event_loop, AllocatorPtr alloc,
		  StopwatchPtr stopwatch,
		  int domain, int type, int protocol,
		  bool ip_transparent,
		  const SocketAddress bind_address,
		  const SocketAddress address,
		  Event::Duration timeout,
		  ConnectSocketHandler &handler,
		  CancellablePointer &cancel_ptr)
try {
	auto [fd, completed] = CreateConnectSocketNonBlock(domain, type, protocol,
							   ip_transparent,
							   bind_address, address);

	if (completed) {
		stopwatch.RecordEvent("connect");

		handler.OnSocketConnectSuccess(std::move(fd));
	} else {
		alloc.New<PConnectSocket>(event_loop,
					  std::move(fd),
					  timeout,
					  std::move(stopwatch),
					  handler, cancel_ptr);
	}
} catch (...) {
	handler.OnSocketConnectError(std::current_exception());
}
