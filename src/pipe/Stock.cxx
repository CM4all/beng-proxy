// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Stock.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "io/UniqueFileDescriptor.hxx"

#ifdef HAVE_URING
#include "io/uring/Close.hxx"
#endif // HAVE_URING

class PipeStockItem final : public StockItem {
	UniqueFileDescriptor fds[2];

public:
	explicit PipeStockItem(CreateStockItem c);

	std::pair<FileDescriptor, FileDescriptor> Get() const noexcept {
		return {fds[0], fds[1]};
	}

	~PipeStockItem() noexcept override;

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

PipeStockItem::~PipeStockItem() noexcept
{
#ifdef HAVE_URING
	const auto &pipe_stock = static_cast<const PipeStock &>(GetStock());
	auto *uring_queue = pipe_stock.GetUringQueue();
	if (uring_queue != nullptr) {
		if (fds[0].IsDefined())
			Uring::Close(uring_queue, fds[0].Release());
		if (fds[1].IsDefined())
			Uring::Close(uring_queue, fds[1].Release());
	}
#endif // HAVE_URING
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
