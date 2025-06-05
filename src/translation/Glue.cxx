// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
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
#include "net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

class TranslationGlue::Request final
	: Cancellable, StockGetHandler, Lease, PoolLeakDetector
{
	const AllocatorPtr alloc;

	[[no_unique_address]]
	StopwatchPtr stopwatch;

	StockItem *item;

	const TranslateRequest &request;

	TranslateHandler &handler;

	CancellablePointer &caller_cancel_ptr;
	CancellablePointer cancel_ptr;

public:
	Request(AllocatorPtr _alloc,
		const TranslateRequest &_request,
		const StopwatchPtr &parent_stopwatch,
		TranslateHandler &_handler,
		CancellablePointer &_cancel_ptr) noexcept
		:PoolLeakDetector(_alloc),
		 alloc(_alloc),
		 stopwatch(parent_stopwatch, "translate",
			   _request.GetDiagnosticName()),
		 request(_request),
		 handler(_handler),
		 caller_cancel_ptr(_cancel_ptr)
	{
		_cancel_ptr = *this;
	}

	void Start(TranslationStock &_stock) noexcept {
		_stock.Get(*this, cancel_ptr);
	}

private:
	void Destroy() noexcept {
		this->~Request();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* this cancels only the TranslationGlue::Get() call initiated
		   from Start() */

		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = *item;
		Destroy();
		return _item.Put(action);
	}
};

void
TranslationGlue::Request::OnStockItemReady(StockItem &_item) noexcept
{
	stopwatch.RecordEvent("connect");

	item = &_item;

	/* cancellation will not be handled by this class from here on;
	   instead, we pass the caller's CancellablePointer to
	   translate() */
	translate(alloc, _item.GetStock().GetEventLoop(), std::move(stopwatch),
		  TranslationStock::GetSocket(_item),
		  *this,
		  request, handler,
		  caller_cancel_ptr);

	/* ReleaseLease() will invoke Destroy() */
}

void
TranslationGlue::Request::OnStockItemError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("connect_error");

	auto &_handler = handler;
	Destroy();
	_handler.OnTranslateError(ep);
}

void
TranslationGlue::SendRequest(AllocatorPtr alloc,
			      const TranslateRequest &request,
			      const StopwatchPtr &parent_stopwatch,
			      TranslateHandler &handler,
			      CancellablePointer &cancel_ptr) noexcept
{
	auto r = alloc.New<Request>(alloc, request,
				    parent_stopwatch,
				    handler, cancel_ptr);
	r->Start(stock);
}
