// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

struct PipeStockItem final : StockItem {
	UniqueFileDescriptor fds[2];

	explicit PipeStockItem(CreateStockItem c) noexcept
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

	/* enlarge the pipe buffer to 256 kB to reduce the number of
	   splice() system calls */
	constexpr unsigned PIPE_BUFFER_SIZE = 256 * 1024;
	item->fds[1].SetPipeCapacity(PIPE_BUFFER_SIZE);

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

std::pair<FileDescriptor, FileDescriptor>
pipe_stock_item_get(StockItem *_item) noexcept
{
	auto *item = (PipeStockItem *)_item;

	return {item->fds[0], item->fds[1]};
}
