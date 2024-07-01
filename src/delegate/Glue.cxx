// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "Stock.hxx"
#include "stock/Item.hxx"
#include "stock/MapStock.hxx"
#include "lease.hxx"
#include "net/SocketDescriptor.hxx"
#include "AllocatorPtr.hxx"

struct DelegateGlue final : Lease {
	StockItem &item;

	explicit DelegateGlue(StockItem &_item):item(_item) {}

	void Destroy() noexcept {
		this->~DelegateGlue();
	}

	void Start(EventLoop &event_loop, AllocatorPtr alloc, const char *path,
		   DelegateHandler &handler, CancellablePointer &cancel_ptr) noexcept {
		delegate_open(event_loop, delegate_stock_item_get(item), *this,
			      alloc, path, handler, cancel_ptr);
	}

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		auto &_item = item;
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
	StockItem *item;

	try {
		item = delegate_stock_get(stock, helper, options);
	} catch (...) {
		handler.OnDelegateError(std::current_exception());
		return;
	}

	auto glue = alloc.New<DelegateGlue>(*item);
	glue->Start(stock->GetEventLoop(), alloc, path, handler, cancel_ptr);
}
