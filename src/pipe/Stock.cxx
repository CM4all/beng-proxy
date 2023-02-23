// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

struct PipeStockItem final : StockItem {
	UniqueFileDescriptor fds[2];

	explicit PipeStockItem(CreateStockItem c)
		:StockItem(c) {
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;
};

/*
 * stock class
 *
 */

void
PipeStock::Create(CreateStockItem c, StockRequest,
		  StockGetHandler &get_handler,
		  CancellablePointer &)
{
	auto *item = new PipeStockItem(c);

	if (!UniqueFileDescriptor::CreatePipeNonBlock(item->fds[0],
						      item->fds[1])) {
		int e = errno;
		delete item;
		throw MakeErrno(e, "pipe() failed");
	}

	item->InvokeCreateSuccess(get_handler);
}

bool
PipeStockItem::Borrow() noexcept
{
	return true;
}

bool
PipeStockItem::Release() noexcept
{
	return true;
}

/*
 * interface
 *
 */

void
pipe_stock_item_get(StockItem *_item, FileDescriptor fds[2]) noexcept
{
	auto *item = (PipeStockItem *)_item;

	fds[0] = item->fds[0];
	fds[1] = item->fds[1];
}
