// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Options.hxx"
#include "io/uring/config.h" // for HAVE_URING

#include <utility> // for std::pair

class FileDescriptor;

#ifdef HAVE_URING
namespace Uring { class Queue; }
#endif

/**
 * Anonymous pipe pooling, to speed to istream_pipe.
 */
class PipeStock final : public Stock, StockClass {
#ifdef HAVE_URING
	Uring::Queue *uring_queue;
#endif // HAVE_URING

public:
	explicit PipeStock(EventLoop &event_loop)
		:Stock(event_loop, *this, "pipe",
		       {.limit = 0, .max_idle = 64}) {}

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &_uring_queue) noexcept {
		uring_queue = &_uring_queue;
	}

	Uring::Queue *GetUringQueue() const noexcept {
		return uring_queue;
	}
#endif // HAVE_URING

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};

[[gnu::pure]]
std::pair<FileDescriptor, FileDescriptor>
pipe_stock_item_get(StockItem *item) noexcept;
