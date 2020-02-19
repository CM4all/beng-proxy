/*
 * Copyright 2007-2018 Content Management AG
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
#include "Factory.hxx"
#include "FilteredSocket.hxx"
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/GetHandler.hxx"
#include "stock/LoggerDomain.hxx"
#include "cluster/AddressList.hxx"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringBuilder.hxx"
#include "util/Exception.hxx"
#include "stopwatch.hxx"

#include <memory>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct FilteredSocketStockRequest {
	StopwatchPtr stopwatch;

	const bool ip_transparent;

	const SocketAddress bind_address, address;

	const Event::Duration timeout;

	SocketFilterFactory *const filter_factory;

	FilteredSocketStockRequest(StopwatchPtr &&_stopwatch,
				   bool _ip_transparent,
				   SocketAddress _bind_address,
				   SocketAddress _address,
				   Event::Duration _timeout,
				   SocketFilterFactory *_filter_factory) noexcept
		:stopwatch(std::move(_stopwatch)),
		 ip_transparent(_ip_transparent),
		 bind_address(_bind_address), address(_address),
		 timeout(_timeout),
		 filter_factory(_filter_factory) {}
};

class FilteredSocketStockConnection final
	: StockItem, ConnectFilteredSocketHandler, BufferedSocketHandler, Cancellable {

	BasicLogger<StockLoggerDomain> logger;

	const AllocatedSocketAddress address;

	/**
	 * To cancel the ClientSocket.
	 */
	CancellablePointer cancel_ptr;

	std::unique_ptr<FilteredSocket> socket;

public:
	FilteredSocketStockConnection(CreateStockItem c,
				      SocketAddress _address,
				      CancellablePointer &_cancel_ptr) noexcept
		:StockItem(c),
		 logger(c.stock),
		 address(_address)
	{
		_cancel_ptr = *this;

		cancel_ptr = nullptr;
	}

	~FilteredSocketStockConnection() override {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	void Start(FilteredSocketStockRequest &&request) noexcept {
		ConnectFilteredSocket(stock.GetEventLoop(),
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
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		assert(cancel_ptr);

		cancel_ptr.CancelAndClear();
		InvokeCreateAborted();
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> socket) noexcept override;
	void OnConnectFilteredSocketError(std::exception_ptr e) noexcept override;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
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
	cancel_ptr = nullptr;

	socket = std::move(_socket);
	socket->Reinit(Event::Duration(-1), Event::Duration(-1),
		       *this);

	InvokeCreateSuccess();
}

void
FilteredSocketStockConnection::OnConnectFilteredSocketError(std::exception_ptr ep) noexcept
{
	cancel_ptr = nullptr;

	ep = NestException(ep,
			   FormatRuntimeError("Failed to connect to '%s'",
					      GetStockName()));
	InvokeCreateError(ep);
}

/*
 * stock class
 *
 */

void
FilteredSocketStock::Create(CreateStockItem c, StockRequest _request,
			    CancellablePointer &cancel_ptr)
{
	auto &request = *(FilteredSocketStockRequest *)_request.get();

	auto *connection = new FilteredSocketStockConnection(c,
							     request.address,
							     cancel_ptr);
	connection->Start(std::move(request));
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

	socket->ScheduleReadTimeout(false, std::chrono::minutes(1));

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
								 ip_transparent,
								 bind_address, address,
								 timeout,
								 filter_factory);

	stock.Get(key, std::move(request), handler, cancel_ptr);
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
