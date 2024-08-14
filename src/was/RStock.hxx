// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Class.hxx"
#include "stock/MultiStock.hxx"

class AllocatorPtr;
class SocketAddress;

class RemoteWasStock final : MultiStockClass {
	class MultiClientStockClass final : public StockClass {
	public:
		/* virtual methods from class StockClass */
		void Create(CreateStockItem c, StockRequest request,
			    StockGetHandler &handler,
			    CancellablePointer &cancel_ptr) override;
	};

	MultiClientStockClass multi_client_stock_class;

	MultiStock multi_stock;

public:
	RemoteWasStock(unsigned limit, unsigned max_idle,
		       EventLoop &event_loop) noexcept;

	auto &GetEventLoop() const noexcept {
		return multi_stock.GetEventLoop();
	}

	void FadeAll() noexcept {
		multi_stock.FadeAll();
	}

	/**
	 * The resulting #StockItem will be a #WasStockConnection
	 * instance.
	 */
	void Get(AllocatorPtr alloc, SocketAddress address,
		 unsigned parallelism, unsigned concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class MultiStockClass */
	std::size_t GetLimit(const void *request,
			     std::size_t _limit) const noexcept override;

	Event::Duration GetClearInterval(const void *) const noexcept override {
		return std::chrono::minutes{5};
	}

	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;
};
