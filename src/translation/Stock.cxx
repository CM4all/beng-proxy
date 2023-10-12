// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "Client.hxx"
#include "stock/Item.hxx"
#include "stock/GetHandler.hxx"
#include "lease.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "lib/fmt/SocketAddressFormatter.hxx"
#include "lib/fmt/SystemError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>
#include <errno.h>

static UniqueSocketDescriptor
CreateConnectStreamSocket(const SocketAddress address)
{
	UniqueSocketDescriptor fd;
	if (!fd.CreateNonBlock(address.GetFamily(), SOCK_STREAM, 0))
		throw MakeErrno("Failed to create socket");

	if (!fd.Connect(address))
		throw FmtErrno("Failed to connect to {}", address);

	return fd;
}

class TranslationStock::Connection final : public StockItem {
	UniqueSocketDescriptor s;

	SocketEvent event;

public:
	Connection(CreateStockItem c, UniqueSocketDescriptor &&_s) noexcept
		:StockItem(c), s(std::move(_s)),
		 event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback), s) {}

	SocketDescriptor GetSocket() noexcept {
		return s;
	}

private:
	void EventCallback(unsigned) noexcept {
		std::byte buffer[1];
		ssize_t nbytes = s.Receive(buffer, MSG_DONTWAIT);
		if (nbytes < 0)
			LogConcat(2, "translation",
				  "error on idle translation server connection: ",
				  strerror(errno));
		else if (nbytes > 0)
			LogConcat(2, "translation",
				  "unexpected data in idle translation server connection");

		InvokeIdleDisconnect();
	}

public:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		return true;
	}
};

class TranslationStock::Request final
	: Cancellable, StockGetHandler, Lease, PoolLeakDetector
{
	const AllocatorPtr alloc;

	StopwatchPtr stopwatch;

	TranslationStock &stock;
	Connection *item;

	const TranslateRequest &request;

	TranslateHandler &handler;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	Request(TranslationStock &_stock, AllocatorPtr _alloc,
		const TranslateRequest &_request,
		const StopwatchPtr &parent_stopwatch,
		TranslateHandler &_handler,
		CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_alloc),
		 alloc(_alloc),
		 stopwatch(parent_stopwatch, "translate",
			   _request.GetDiagnosticName()),
		 stock(_stock),
		 request(_request),
		 handler(_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		_cancel_ptr = *this;
	}

	void Start() noexcept {
		stock.Get(*this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		this->~Request();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* this cancels only the TranslationStock::Get() call initiated
		   from Start() */

		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_stock = stock;
		auto &_item = *item;
		Destroy();
		return _stock.Put(_item, action);
	}
};

/*
 * stock callback
 *
 */

void
TranslationStock::Request::OnStockItemReady(StockItem &_item) noexcept
{
	stopwatch.RecordEvent("connect");

	item = &(Connection &)_item;

	/* cancellation will not be handled by this class from here on;
	   instead, we pass the caller's CancellablePointer to
	   translate() */
	translate(alloc, stock.GetEventLoop(), std::move(stopwatch),
		  item->GetSocket(),
		  *this,
		  request, handler,
		  caller_cancel_ptr);

	/* ReleaseLease() will invoke Destroy() */
}

void
TranslationStock::Request::OnStockItemError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	auto &_handler = handler;
	Destroy();
	_handler.OnTranslateError(ep);
}

void
TranslationStock::Create(CreateStockItem c, StockRequest,
			 StockGetHandler &handler,
			 CancellablePointer &)
{
	auto *connection = new Connection(c,
					  CreateConnectStreamSocket(address));
	connection->InvokeCreateSuccess(handler);
}

void
TranslationStock::SendRequest(AllocatorPtr alloc,
			      const TranslateRequest &request,
			      const StopwatchPtr &parent_stopwatch,
			      TranslateHandler &handler,
			      CancellablePointer &cancel_ptr) noexcept
{
	auto r = alloc.New<Request>(*this, alloc, request,
				    parent_stopwatch,
				    handler, cancel_ptr);
	r->Start();
}
