/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Stock.hxx"
#include "Key.hxx"
#include "Connect.hxx"
#include "FilteredSocket.hxx"
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "stock/Stock.hxx"
#include "stock/GetHandler.hxx"
#include "stock/LoggerDomain.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringBuilder.hxx"
#include "util/Exception.hxx"
#include "stopwatch.hxx"

#include <cassert>
#include <memory>

struct FilteredSocketStockRequest {
	StopwatchPtr stopwatch;

	const uint_least64_t fairness_hash;

	const bool ip_transparent;

	const SocketAddress bind_address, address;

	const Event::Duration timeout;

	SocketFilterFactory *const filter_factory;

	FilteredSocketStockRequest(StopwatchPtr &&_stopwatch,
				   uint_fast64_t _fairness_hash,
				   bool _ip_transparent,
				   SocketAddress _bind_address,
				   SocketAddress _address,
				   Event::Duration _timeout,
				   SocketFilterFactory *_filter_factory) noexcept
		:stopwatch(std::move(_stopwatch)),
		 fairness_hash(_fairness_hash),
		 ip_transparent(_ip_transparent),
		 bind_address(_bind_address), address(_address),
		 timeout(_timeout),
		 filter_factory(_filter_factory) {}
};

class FilteredSocketStockConnection final
	: public StockItem, ConnectFilteredSocketHandler,
	  BufferedSocketHandler, Cancellable {

	BasicLogger<StockLoggerDomain> logger;

	const AllocatedSocketAddress address;

	StockGetHandler *const handler;

	/**
	 * To cancel the ClientSocket.
	 */
	CancellablePointer cancel_ptr;

	std::unique_ptr<FilteredSocket> socket;

	CoarseTimerEvent idle_timer;

public:
	FilteredSocketStockConnection(CreateStockItem c,
				      SocketAddress _address,
				      StockGetHandler &_handler,
				      CancellablePointer &_cancel_ptr) noexcept
		:StockItem(c),
		 logger(c.stock),
		 address(_address),
		 handler(&_handler),
		 idle_timer(c.stock.GetEventLoop(),
			    BIND_THIS_METHOD(OnIdleTimeout))
	{
		_cancel_ptr = *this;

		cancel_ptr = nullptr;
	}

	FilteredSocketStockConnection(CreateStockItem c,
				      SocketAddress _address,
				      std::unique_ptr<FilteredSocket> &&_socket) noexcept
		:StockItem(c),
		 logger(c.stock),
		 address(_address),
		 handler(nullptr),
		 socket(std::move(_socket)),
		 idle_timer(c.stock.GetEventLoop(),
			    BIND_THIS_METHOD(OnIdleTimeout))
	{
	}

	~FilteredSocketStockConnection() override {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	auto &GetEventLoop() const noexcept {
		return idle_timer.GetEventLoop();
	}

	void Start(FilteredSocketStockRequest &&request) noexcept {
		ConnectFilteredSocket(GetEventLoop(),
				      std::move(request.stopwatch),
				      request.ip_transparent,
				      request.bind_address,
				      request.address,
				      request.timeout,
				      request.filter_factory,
				      *this, cancel_ptr);
	}

	SocketAddress GetAddress() const noexcept {
		return address;
	}

	auto &GetSocket() noexcept {
		assert(socket);

		return *socket;
	}

private:
	void OnIdleTimeout() noexcept {
		InvokeIdleDisconnect();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(cancel_ptr);

		cancel_ptr.Cancel();
		InvokeCreateAborted();
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> socket) noexcept override;
	void OnConnectFilteredSocketError(std::exception_ptr e) noexcept override;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;

	gcc_noreturn
	bool OnBufferedWrite() override {
		/* should never be reached because we never schedule
		   writing */
		gcc_unreachable();
	}

	void OnBufferedError(std::exception_ptr e) noexcept override;

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		idle_timer.Cancel();
		return true;
	}

	bool Release() noexcept override;
};

/*
 * BufferedSocketHandler
 *
 */

BufferedResult
FilteredSocketStockConnection::OnBufferedData()
{
	logger(2, "unexpected data in idle TCP connection");
	InvokeIdleDisconnect();
	return BufferedResult::CLOSED;
}

bool
FilteredSocketStockConnection::OnBufferedHangup() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

bool
FilteredSocketStockConnection::OnBufferedClosed() noexcept
{
	InvokeIdleDisconnect();
	return false;
}

void
FilteredSocketStockConnection::OnBufferedError(std::exception_ptr e) noexcept
{
	logger(2, "error on idle connection: ", e);
	InvokeIdleDisconnect();
}

/*
 * client_socket callback
 *
 */

void
FilteredSocketStockConnection::OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> _socket) noexcept
{
	assert(handler != nullptr);

	cancel_ptr = nullptr;

	socket = std::move(_socket);
	socket->Reinit(Event::Duration(-1), Event::Duration(-1),
		       *this);

	InvokeCreateSuccess(*handler);
}

void
FilteredSocketStockConnection::OnConnectFilteredSocketError(std::exception_ptr ep) noexcept
{
	assert(handler != nullptr);

	cancel_ptr = nullptr;

	ep = NestException(ep,
			   FormatRuntimeError("Failed to connect to '%s'",
					      GetStockName()));
	InvokeCreateError(*handler, std::move(ep));
}

/*
 * stock class
 *
 */

void
FilteredSocketStock::Create(CreateStockItem c, StockRequest _request,
			    StockGetHandler &handler,
			    CancellablePointer &cancel_ptr)
{
	/* move the request to the stack to avoid use-after-free in
	   the StockRequest destructor if the pool gets destroyed
	   before this method returns */
	auto request = std::move(*(FilteredSocketStockRequest *)_request.get());
	_request.reset();

	auto *connection = new FilteredSocketStockConnection(c,
							     request.address,
							     handler, cancel_ptr);
	connection->Start(std::move(request));
}

uint_fast64_t
FilteredSocketStock::GetFairnessHash(const void *_request) const noexcept
{
	const auto &request = *(const FilteredSocketStockRequest *)_request;
	return request.fairness_hash;
}

bool
FilteredSocketStockConnection::Release() noexcept
{
	assert(socket);

	if (!socket->IsConnected())
		return false;

	if (!socket->IsEmpty()) {
		logger(2, "unexpected data in idle connection");
		return false;
	}

	socket->Reinit(Event::Duration(-1), Event::Duration(-1), *this);
	socket->UnscheduleWrite();

	socket->ScheduleReadNoTimeout(false);
	idle_timer.Schedule(std::chrono::minutes(1));

	return true;
}

/*
 * interface
 *
 */

void
FilteredSocketStock::Get(AllocatorPtr alloc,
			 StopwatchPtr stopwatch,
			 const char *name,
			 uint_fast64_t fairness_hash,
			 bool ip_transparent,
			 SocketAddress bind_address,
			 SocketAddress address,
			 Event::Duration timeout,
			 SocketFilterFactory *filter_factory,
			 StockGetHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept
{
	assert(!address.IsNull());

	char key_buffer[1024];
	try {
		StringBuilder b(key_buffer);
		MakeFilteredSocketStockKey(b, name, bind_address, address,
					   filter_factory);
	} catch (StringBuilder::Overflow) {
		/* shouldn't happen */
		handler.OnStockItemError(std::current_exception());
		return;
	}

	const char *key = key_buffer;

	auto request =
		NewDisposablePointer<FilteredSocketStockRequest>(alloc,
								 std::move(stopwatch),
								 fairness_hash,
								 ip_transparent,
								 bind_address, address,
								 timeout,
								 filter_factory);

	stock.Get(key, std::move(request), handler, cancel_ptr);
}

void
FilteredSocketStock::Add(const char *key, SocketAddress address,
			 std::unique_ptr<FilteredSocket> socket) noexcept
{
	auto &_stock = stock.GetStock(key, nullptr);

	const CreateStockItem c{_stock};

	auto *connection = new FilteredSocketStockConnection(c, address,
							     std::move(socket));
	_stock.InjectIdle(*connection);
}

FilteredSocket &
fs_stock_item_get(StockItem &item)
{
	auto &connection = (FilteredSocketStockConnection &)item;

	return connection.GetSocket();
}

SocketAddress
fs_stock_item_get_address(const StockItem &item)
{
	const auto &connection = (const FilteredSocketStockConnection &)item;

	return connection.GetAddress();
}
