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

	/* virtual methods from class Lease */
	void ReleaseLease(bool reuse) noexcept override {
		item.Put(!reuse);
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
	delegate_open(stock->GetEventLoop(), delegate_stock_item_get(*item), *glue,
		      alloc, path,
		      handler, cancel_ptr);
}
