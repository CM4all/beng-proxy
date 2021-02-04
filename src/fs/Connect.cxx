/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Connect.hxx"
#include "FilteredSocket.hxx"
#include "Factory.hxx"
#include "event/net/ConnectSocket.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/LeakDetector.hxx"
#include "stopwatch.hxx"

#include <exception>

#include <netinet/in.h>

class ConnectFilteredSocketOperation final
	: Cancellable, ConnectSocketHandler, BufferedSocketHandler,
	  LeakDetector
{
	ConnectFilteredSocketHandler &handler;

	const StopwatchPtr stopwatch;

	CoarseTimerEvent timeout_event;

	DeferEvent defer_handshake_callback;

	ConnectSocket connect_socket;

	SocketFilterFactory *const filter_factory;

	std::unique_ptr<FilteredSocket> socket;

	FdType fd_type;

public:
	ConnectFilteredSocketOperation(EventLoop &event_loop,
				       SocketFilterFactory *_filter_factory,
				       StopwatchPtr &&_stopwatch,
				       ConnectFilteredSocketHandler &_handler,
				       CancellablePointer &caller_cancel_ptr) noexcept
		:handler(_handler), stopwatch(std::move(_stopwatch)),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
		 defer_handshake_callback(event_loop,
					  BIND_THIS_METHOD(OnDeferredHandshake)),
		 connect_socket(event_loop, *this),
		 filter_factory(_filter_factory)
	{
		caller_cancel_ptr = *this;
	}

	auto &GetEventLoop() const noexcept {
		return connect_socket.GetEventLoop();
	}

	void Start(bool ip_transparent,
		   SocketAddress bind_address,
		   SocketAddress address,
		   Event::Duration timeout) noexcept;

private:
	void OnHandshake() noexcept;
	void OnDeferredHandshake() noexcept;
	void OnTimeout() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		stopwatch.RecordEvent("cancel");

		if (!socket)
			connect_socket.Cancel();

		delete this;
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;

	void OnSocketConnectError(std::exception_ptr e) noexcept override {
		stopwatch.RecordEvent("error");
		handler.OnConnectFilteredSocketError(std::move(e));
		delete this;
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedClosed() noexcept override;

	bool OnBufferedWrite() override {
		/* we called ScheduleWrite() only to initiate the TLS
		   handshake; we don't need to write, actually */
		// TODO: move this to the SslFilter
		socket->UnscheduleWrite();
		return true;
	}

	void OnBufferedError(std::exception_ptr e) noexcept override;
};

void
ConnectFilteredSocketOperation::Start(bool ip_transparent,
				      SocketAddress bind_address,
				      SocketAddress address,
				      Event::Duration timeout) noexcept
try {
	const int address_family = address.GetFamily();
	fd_type = address_family == AF_LOCAL
		? FD_SOCKET
		: FD_TCP;

	UniqueSocketDescriptor fd;

	if (!fd.CreateNonBlock(address_family, SOCK_STREAM, 0))
		throw MakeErrno("Failed to create socket");

	if ((address_family == PF_INET || address_family == PF_INET6) &&
	    !fd.SetNoDelay())
		throw MakeErrno("Failed to set TCP_NODELAY");

	if (ip_transparent && !fd.SetBoolOption(SOL_IP, IP_TRANSPARENT, true))
		throw MakeErrno("Failed to set IP_TRANSPARENT");

	if (!bind_address.IsNull() && bind_address.IsDefined() &&
	    !fd.Bind(bind_address))
		throw MakeErrno("Failed to bind socket");

	if (fd.Connect(address)) {
		OnSocketConnectSuccess(std::move(fd));
		return;
	}

	if (errno != EINPROGRESS)
		throw MakeErrno("Failed to connect");

	connect_socket.WaitConnected(std::move(fd), Event::Duration(-1));
	timeout_event.Schedule(timeout);
} catch (...) {
	stopwatch.RecordEvent("error");
	handler.OnConnectFilteredSocketError(std::current_exception());
	delete this;
}

void
ConnectFilteredSocketOperation::OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept
{
	stopwatch.RecordEvent("connect");

	socket = std::make_unique<FilteredSocket>(GetEventLoop());

	try {
		socket->Init(fd.Release(), fd_type,
			     Event::Duration(-1), Event::Duration(-1),
			     filter_factory != nullptr
			     ? filter_factory->CreateFilter()
			     : nullptr,
			     *this);
	} catch (...) {
		stopwatch.RecordEvent("error");
		handler.OnConnectFilteredSocketError(std::current_exception());
		delete this;
	}

	if (filter_factory) {
		socket->ScheduleWrite();
		socket->SetHandshakeCallback(BIND_THIS_METHOD(OnHandshake));
	} else {
		handler.OnConnectFilteredSocket(std::move(socket));
		delete this;
	}
}

BufferedResult
ConnectFilteredSocketOperation::OnBufferedData()
{
	return BufferedResult::BLOCKING;
}

bool
ConnectFilteredSocketOperation::OnBufferedClosed() noexcept
{
	stopwatch.RecordEvent("error");
	handler.OnConnectFilteredSocketError(std::make_exception_ptr(std::runtime_error("Peer closed the connection prematurely")));
	delete this;
	return false;
}

void
ConnectFilteredSocketOperation::OnBufferedError(std::exception_ptr e) noexcept
{
	stopwatch.RecordEvent("error");
	handler.OnConnectFilteredSocketError(std::move(e));
	delete this;
}

void
ConnectFilteredSocketOperation::OnHandshake() noexcept
{
	assert(socket);
	assert(socket->IsConnected());

	stopwatch.RecordEvent("handshake");

	/* the ThreadSocketFilter::mutex is locked in here, so we need
	   to move the handler callback out of this stack frame */
	defer_handshake_callback.Schedule();
}

void
ConnectFilteredSocketOperation::OnDeferredHandshake() noexcept
{
	assert(socket);
	assert(socket->IsConnected());

	handler.OnConnectFilteredSocket(std::move(socket));
	delete this;
}

void
ConnectFilteredSocketOperation::OnTimeout() noexcept
{
	stopwatch.RecordEvent("timeout");
	handler.OnConnectFilteredSocketError(std::make_exception_ptr(std::runtime_error("Timeout")));
	delete this;
}

void
ConnectFilteredSocket(EventLoop &event_loop,
		      StopwatchPtr stopwatch,
		      bool ip_transparent,
		      SocketAddress bind_address,
		      SocketAddress address,
		      Event::Duration timeout,
		      SocketFilterFactory *filter_factory,
		      ConnectFilteredSocketHandler &handler,
		      CancellablePointer &cancel_ptr) noexcept
{
	auto *cfs = new ConnectFilteredSocketOperation(event_loop,
						       filter_factory,
						       std::move(stopwatch),
						       handler, cancel_ptr);
	cfs->Start(ip_transparent, bind_address, address, timeout);
}
