// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Stock.hxx"
#include "stock/Class.hxx"

#include <utility> // for std::pair

class FileDescriptor;

/**
 * Anonymous pipe pooling, to speed to istream_pipe.
 */
class PipeStock final : public Stock, StockClass {
public:
	explicit PipeStock(EventLoop &event_loop)
		:Stock(event_loop, *this, "pipe", 0, 64,
		       Event::Duration::zero()) {}

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};

[[gnu::pure]]
std::pair<FileDescriptor, FileDescriptor>
pipe_stock_item_get(StockItem *item) noexcept;
