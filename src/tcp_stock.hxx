// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * TCP client connection pooling.
 */

#pragma once

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "event/Chrono.hxx"

class AllocatorPtr;
class SocketDescriptor;
class CancellablePointer;
class SocketAddress;
class EventLoop;
class StopwatchPtr;

/**
 * A TCP connection stock.
 *
 * @return the new TCP connections stock (this function cannot fail)
 */
class TcpStock final : StockClass {
	StockMap stock;

public:
	/**
	 * @param limit the maximum number of connections per host
	 */
	TcpStock(EventLoop &event_loop, std::size_t limit) noexcept
		:stock(event_loop, *this, limit, 16,
		       /* each TcpStockConnection has its own timer */
		       Event::Duration::zero()) {}

	EventLoop &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	void AddStats(StockStats &data) const noexcept {
		stock.AddStats(data);
	}

	/**
	 * @param name the MapStock name; it is auto-generated from the
	 * #address if nullptr is passed here
	 * @param timeout the connect timeout in seconds
	 */
	void Get(AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 const char *name,
		 bool ip_transparent,
		 SocketAddress bind_address,
		 SocketAddress address,
		 Event::Duration timeout,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};

[[gnu::pure]]
SocketDescriptor
tcp_stock_item_get(const StockItem &item) noexcept;

/**
 * Returns the (peer) address this object is connected to.
 */
[[gnu::pure]]
SocketAddress
tcp_stock_item_get_address(const StockItem &item) noexcept;

[[gnu::pure]]
int
tcp_stock_item_get_domain(const StockItem &item) noexcept;
