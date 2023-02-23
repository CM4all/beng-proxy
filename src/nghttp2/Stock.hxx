// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Chrono.hxx"
#include "util/IntrusiveHashSet.hxx"

#include <exception>
#include <memory>

class EventLoop;
class AllocatorPtr;
class StopwatchPtr;
class SocketAddress;
class SocketFilterFactory;
class FilteredSocket;
class CancellablePointer;

namespace NgHttp2 {

class ClientConnection;

class StockGetHandler {
public:
	virtual void OnNgHttp2StockReady(ClientConnection &connection) noexcept = 0;

	/**
	 * The server refuses to accept TLS ALPN "h2", and the handler
	 * may decide to use the socket for something else
	 * (e.g. invoke a HTTP/1.1 client).
	 *
	 * @param socket the connected socket; may be nullptr if
	 * another #StockGetHandler (for the same server) has already
	 * consumed it
	 */
	virtual void OnNgHttp2StockAlpn(std::unique_ptr<FilteredSocket> &&socket) noexcept = 0;

	virtual void OnNgHttp2StockError(std::exception_ptr e) noexcept = 0;
};

class Stock {
	class Item;

	struct ItemHash {
		[[gnu::pure]]
		size_t operator()(const char *key) const noexcept;

		[[gnu::pure]]
		size_t operator()(const Item &item) const noexcept;
	};

	struct ItemEqual {
		[[gnu::pure]]
		bool operator()(const char *a, const Item &b) const noexcept;

		[[gnu::pure]]
		bool operator()(const Item &a, const Item &b) const noexcept;
	};

	using Set = IntrusiveHashSet<Item, 3779, ItemHash, ItemEqual>;
	Set items;

public:
	Stock() noexcept;
	~Stock() noexcept;

	Stock(const Stock &) = delete;
	Stock &operator=(const Stock &) = delete;

	void FadeAll() noexcept;

	void Get(EventLoop &event_loop,
		 AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 const char *name,
		 SocketAddress bind_address,
		 SocketAddress address,
		 Event::Duration timeout,
		 SocketFilterFactory *filter_factory,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Add a newly connected HTTP/2 connection to the stock and
	 * invoke the given #StockGetHandler.
	 *
	 * @param key a string generated with MakeFilteredSocketStockKey()
	 */
	void Add(EventLoop &event_loop,
		 const char *key,
		 std::unique_ptr<FilteredSocket> socket,
		 StockGetHandler &handler) noexcept;

private:
	void DeleteItem(Item *item) noexcept;
};

} // namespace NgHttp2
