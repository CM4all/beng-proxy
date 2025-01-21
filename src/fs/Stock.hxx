// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Stock for FilteredSocket instances.
 */

#pragma once

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"

#include <memory>

class StockItem;
class StockGetHandler;
class CancellablePointer;
class SocketFilterParams;
class FilteredSocket;
class EventLoop;
class SocketAddress;
class StopwatchPtr;
class AllocatorPtr;

/**
 * A stock for TCP connections wrapped with #FilteredSocket.
 */
class FilteredSocketStock final : StockClass {
	StockMap stock;

public:
	/**
	 * @param limit the maximum number of connections per host
	 */
	FilteredSocketStock(EventLoop &event_loop,
			    std::size_t limit, std::size_t max_idle) noexcept
		:stock(event_loop, *this, limit, max_idle,
		       std::chrono::minutes(5)) {}

	EventLoop &GetEventLoop() noexcept {
		return stock.GetEventLoop();
	}

	void AddStats(StockStats &data) const noexcept {
		stock.AddStats(data);
	}

	void FadeAll() noexcept {
		stock.FadeAll();
	}

	/**
	 * @param name the MapStock name; it is auto-generated from the
	 * #address if nullptr is passed here
	 *
	 * @param fairness_hash if non-zero, then two consecutive
	 * requests with the same value are avoided (for fair
	 * scheduling)
	 *
	 * @param timeout the connect timeout in seconds
	 */
	void Get(AllocatorPtr alloc,
		 StopwatchPtr stopwatch,
		 std::string_view name,
		 uint_fast64_t fairness_hash,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 SocketAddress address,
		 Event::Duration timeout,
		 const SocketFilterParams *filter_params,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Add a newly connected socket to the stock.
	 *
	 * @param key a string generated with MakeFilteredSocketStockKey()
	 */
	void Add(StockKey key, SocketAddress address,
		 std::unique_ptr<FilteredSocket> socket) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;

	bool ShouldContinueOnCancel(const void *request) const noexcept override;

	uint_fast64_t GetFairnessHash(const void *request) const noexcept override;
};

[[gnu::pure]]
FilteredSocket &
fs_stock_item_get(StockItem &item);

/**
 * Returns the (peer) address this object is connected to.
 */
[[gnu::pure]]
SocketAddress
fs_stock_item_get_address(const StockItem &item);
