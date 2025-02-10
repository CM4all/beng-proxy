// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Connect.hxx"
#include "FilteredSocket.hxx"
#include "Factory.hxx"
#include "event/net/ConnectSocket.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/DeferEvent.hxx"
#include "net/ConnectSocketX.hxx"
#include "net/SocketAddress.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/TimeoutError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
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

	const SocketFilterFactoryPtr filter_factory;

	std::unique_ptr<FilteredSocket> socket;

	FdType fd_type;

public:
	ConnectFilteredSocketOperation(EventLoop &event_loop,
				       SocketFilterFactoryPtr &&_filter_factory,
				       StopwatchPtr &&_stopwatch,
				       ConnectFilteredSocketHandler &_handler,
				       CancellablePointer &caller_cancel_ptr) noexcept
		:handler(_handler), stopwatch(std::move(_stopwatch)),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
		 defer_handshake_callback(event_loop,
					  BIND_THIS_METHOD(OnDeferredHandshake)),
		 connect_socket(event_loop, *this),
		 filter_factory(std::move(_filter_factory))
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
	bool OnBufferedHangup() noexcept override;
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

	auto [fd, completed] = CreateConnectSocketNonBlock(address_family, SOCK_STREAM, 0,
							   ip_transparent,
							   bind_address,
							   address);
	if (completed) {
		OnSocketConnectSuccess(std::move(fd));
	} else {
		connect_socket.WaitConnected(std::move(fd), Event::Duration(-1));
		timeout_event.Schedule(timeout);
	}
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
		socket->Init(std::move(fd), fd_type,
			     Event::Duration(-1),
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
	return BufferedResult::OK;
}

bool
ConnectFilteredSocketOperation::OnBufferedHangup() noexcept
{
	stopwatch.RecordEvent("error");
	handler.OnConnectFilteredSocketError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
	delete this;
	return false;
}

bool
ConnectFilteredSocketOperation::OnBufferedClosed() noexcept
{
	stopwatch.RecordEvent("error");
	handler.OnConnectFilteredSocketError(std::make_exception_ptr(SocketClosedPrematurelyError{}));
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
	handler.OnConnectFilteredSocketError(std::make_exception_ptr(TimeoutError{"Connect timeout"}));
	delete this;
}

void
ConnectFilteredSocket(EventLoop &event_loop,
		      StopwatchPtr stopwatch,
		      bool ip_transparent,
		      SocketAddress bind_address,
		      SocketAddress address,
		      Event::Duration timeout,
		      SocketFilterFactoryPtr filter_factory,
		      ConnectFilteredSocketHandler &handler,
		      CancellablePointer &cancel_ptr) noexcept
{
	auto *cfs = new ConnectFilteredSocketOperation(event_loop,
						       std::move(filter_factory),
						       std::move(stopwatch),
						       handler, cancel_ptr);
	cfs->Start(ip_transparent, bind_address, address, timeout);
}
