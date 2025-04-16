// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

class PipeStockItem final : public StockItem {
	UniqueFileDescriptor fds[2];

public:
	explicit PipeStockItem(CreateStockItem c);

	std::pair<FileDescriptor, FileDescriptor> Get() const noexcept {
		return {fds[0], fds[1]};
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;
};

inline
PipeStockItem::PipeStockItem(CreateStockItem c)
	:StockItem(c)
{
	if (!UniqueFileDescriptor::CreatePipeNonBlock(fds[0], fds[1]))
		throw MakeErrno("pipe() failed");

	/* enlarge the pipe buffer to 256 kB to reduce the number of
	   splice() system calls */
	constexpr unsigned PIPE_BUFFER_SIZE = 256 * 1024;
	fds[1].SetPipeCapacity(PIPE_BUFFER_SIZE);
}

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
	return item->Get();
}
