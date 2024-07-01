// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "Stock.hxx"
#include "stock/Item.hxx"
#include "stock/MapStock.hxx"
#include "stock/GetHandler.hxx"
#include "lease.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

struct DelegateGlue final : Cancellable, StockGetHandler, Lease {
	EventLoop &event_loop;
	const AllocatorPtr alloc;
	const char *const path;
	DelegateHandler &handler;

	CancellablePointer cancel_ptr;

	StockItem *item = nullptr;

	DelegateGlue(EventLoop &_event_loop, AllocatorPtr _alloc,
		     const char *_path, DelegateHandler &_handler) noexcept
		:event_loop(_event_loop), alloc(_alloc),
		 path(_path), handler(_handler) {}

	void Destroy() noexcept {
		this->~DelegateGlue();
	}

	void Start(StockMap &stock,
		   const char *helper, const ChildOptions &options,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;

		delegate_stock_get(stock, alloc, helper, options,
				   *this, cancel_ptr);
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &_item) noexcept override {
		item = &_item;

		delegate_open(event_loop, delegate_stock_item_get(_item), *this,
			      alloc, path, handler, cancel_ptr);
	}

	void OnStockItemError(std::exception_ptr error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnDelegateError(std::move(error));
	}

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = *item;
		Destroy();
		return _item.Put(action);
	}
};

void
delegate_stock_open(StockMap *stock, AllocatorPtr alloc,
		    const char *helper,
		    const ChildOptions &options,
		    const char *path,
		    DelegateHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	auto *glue = alloc.New<DelegateGlue>(stock->GetEventLoop(), alloc, path, handler);
	glue->Start(*stock, helper, options, cancel_ptr);
}
