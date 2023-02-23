// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

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
{
	assert(!address.IsNull());

	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(domain, type, protocol)) {
		handler.OnSocketConnectError(std::make_exception_ptr(MakeSocketError("Failed to create socket")));
		return;
	}

	if ((domain == PF_INET || domain == PF_INET6) && type == SOCK_STREAM &&
	    !fd.SetNoDelay()) {
		handler.OnSocketConnectError(std::make_exception_ptr(MakeSocketError("Failed to set TCP_NODELAY")));
		return;
	}

	if (ip_transparent && !fd.SetBoolOption(SOL_IP, IP_TRANSPARENT, true)) {
		handler.OnSocketConnectError(std::make_exception_ptr(MakeSocketError("Failed to set IP_TRANSPARENT")));
		return;
	}

	if (!bind_address.IsNull() && bind_address.IsDefined()) {
		if (bind_address.HasPort() && bind_address.GetPort() == 0)
			/* delay port allocation to avoid running out
			   of ports (EADDRINUSE) */
			fd.SetBoolOption(SOL_IP, IP_BIND_ADDRESS_NO_PORT,
					 true);

		if (!fd.Bind(bind_address)) {
			handler.OnSocketConnectError(std::make_exception_ptr(MakeSocketError("Failed to bind socket")));
			return;
		}
	}

	if (fd.Connect(address)) {
		stopwatch.RecordEvent("connect");

		handler.OnSocketConnectSuccess(std::move(fd));
	} else {
		const auto e = GetSocketError();
		if (IsSocketErrorConnectWouldBlock(e))
			alloc.New<PConnectSocket>(event_loop,
						  std::move(fd), timeout,
						  std::move(stopwatch),
						  handler, cancel_ptr);
		else
			handler.OnSocketConnectError(std::make_exception_ptr(MakeSocketError(e, "Failed to connect")));
	}
}
