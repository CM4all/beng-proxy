// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "tcp_stock.hxx"
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "stock/LoggerDomain.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/net/BufferedSocket.hxx"
#include "net/PConnectSocket.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/SocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/FormatAddress.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "stopwatch.hxx"

#include <utility> // for std::unreachable()

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct TcpStockRequest {
	AllocatorPtr alloc;

	StopwatchPtr stopwatch;

	const bool ip_transparent;

	const SocketAddress bind_address, address;

	const Event::Duration timeout;

	TcpStockRequest(AllocatorPtr _alloc, const StopwatchPtr &parent_stopwatch,
			std::string_view name,
			bool _ip_transparent, SocketAddress _bind_address,
			SocketAddress _address, Event::Duration _timeout) noexcept
		:alloc(_alloc),
		 stopwatch(parent_stopwatch, name),
		 ip_transparent(_ip_transparent), bind_address(_bind_address),
		 address(_address), timeout(_timeout) {}
};

struct TcpStockConnection final
	: StockItem, ConnectSocketHandler, BufferedSocketHandler, Cancellable {

	BasicLogger<StockLoggerDomain> logger;

	StockGetHandler &handler;

	/**
	 * To cancel the ClientSocket.
	 */
	CancellablePointer cancel_ptr;

	BufferedSocket socket;
	CoarseTimerEvent idle_timer;

	FdType fd_type;

	TcpStockConnection(CreateStockItem c,
			   FdType _fd_type,
			   StockGetHandler &_handler,
			   CancellablePointer &_cancel_ptr) noexcept
		:StockItem(c),
		 logger(c.stock),
		 handler(_handler),
		 socket(c.stock.GetEventLoop()),
		 idle_timer(c.stock.GetEventLoop(),
			    BIND_THIS_METHOD(OnIdleTimeout)),
		 fd_type(_fd_type)
	{
		_cancel_ptr = *this;
	}

	~TcpStockConnection() noexcept override;

private:
	void OnIdleTimeout() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(cancel_ptr);

		// our destructor will call cancel_ptr.Cancel()
		delete this;
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
	void OnSocketConnectError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	[[noreturn]] bool OnBufferedWrite() override;
	void OnBufferedError(std::exception_ptr e) noexcept override;

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		if (socket.GetReadyFlags() != 0) [[unlikely]] {
			/* this connection was probably closed, but
			   our SocketEvent callback hasn't been
			   invoked yet; refuse to use this item; the
			   caller will destroy the connection */
			return false;
		}

		idle_timer.Cancel();
		return true;
	}

	bool Release() noexcept override {
		socket.Reinit(Event::Duration(-1), *this);
		socket.UnscheduleWrite();
		socket.ScheduleRead();
		idle_timer.Schedule(std::chrono::minutes(1));
		return true;
	}
};

BufferedResult
TcpStockConnection::OnBufferedData()
{
	logger(2, "unexpected data from idle TCP connection");
	InvokeIdleDisconnect();
	return BufferedResult::DESTROYED;
}

bool
TcpStockConnection::OnBufferedHangup() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
TcpStockConnection::OnBufferedClosed() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
TcpStockConnection::OnBufferedWrite()
{
	/* should never be reached because we never schedule
	   writing */
	std::unreachable();
}

void
TcpStockConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	logger(2, "error on idle TCP connection: ", std::move(e));
	InvokeIdleDisconnect();
}

inline void
TcpStockConnection::OnIdleTimeout() noexcept
{
	InvokeIdleDisconnect();
}


/*
 * client_socket callback
 *
 */

void
TcpStockConnection::OnSocketConnectSuccess(UniqueSocketDescriptor new_fd) noexcept
{
	cancel_ptr = nullptr;

	socket.Init(new_fd.Release(), fd_type,
		    Event::Duration{-1}, *this);
	socket.ScheduleRead();

	InvokeCreateSuccess(handler);
}

void
TcpStockConnection::OnSocketConnectError(std::exception_ptr ep) noexcept
{
	cancel_ptr = nullptr;

	ep = NestException(ep,
			   FmtRuntimeError("Failed to connect to '{}'",
					   GetStockNameView()));
	InvokeCreateError(handler, ep);
}

/*
 * stock class
 *
 */

void
TcpStock::Create(CreateStockItem c,
		 StockRequest _request,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr)
{
	/* move the request to the stack to avoid use-after-free in
	   the StockRequest destructor if the pool gets destroyed
	   before this method returns */
	auto request = std::move(*(TcpStockRequest *)_request.get());
	_request.reset();

	auto *connection = new TcpStockConnection(c,
						  request.address.GetFamily() == AF_LOCAL ? FdType::FD_SOCKET : FdType::FD_TCP,
						  handler,
						  cancel_ptr);

	client_socket_new(c.stock.GetEventLoop(), request.alloc,
			  std::move(request.stopwatch),
			  request.address.GetFamily(), SOCK_STREAM, 0,
			  request.ip_transparent,
			  request.bind_address,
			  request.address,
			  request.timeout,
			  *connection,
			  connection->cancel_ptr);
}

TcpStockConnection::~TcpStockConnection() noexcept
{
	if (cancel_ptr)
		cancel_ptr.Cancel();
}

/*
 * interface
 *
 */

void
TcpStock::Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
	      std::string_view name,
	      bool ip_transparent,
	      SocketAddress bind_address,
	      SocketAddress address,
	      Event::Duration timeout,
	      StockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	assert(!address.IsNull());

	if (name.empty()) {
		char buffer[1024];
		if (!ToString(buffer, address))
			buffer[0] = 0;

		if (!bind_address.IsNull()) {
			char bind_buffer[1024];
			if (!ToString(bind_buffer, bind_address))
				bind_buffer[0] = 0;
			name = alloc.Concat(bind_buffer, ">", buffer);
		} else
			name = alloc.Dup(buffer);
	}

	auto request = NewDisposablePointer<TcpStockRequest>(alloc, alloc,
							     parent_stopwatch,
							     name,
							     ip_transparent,
							     bind_address, address,
							     timeout);

	stock.Get(StockKey{name}, std::move(request), handler, cancel_ptr);
}

BufferedSocket &
tcp_stock_item_get(StockItem &item) noexcept
{
	auto &connection = static_cast<TcpStockConnection &>(item);

	return connection.socket;
}
