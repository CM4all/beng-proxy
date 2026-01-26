// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/Chrono.hxx"
#include "util/IntrusiveHashSet.hxx"

#include <exception>
#include <memory>
#include <string_view>

class EventLoop;
class AllocatorPtr;
class StopwatchPtr;
class SocketAddress;
class SocketFilterParams;
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

	struct ItemGetKey {
		[[gnu::pure]]
		std::string_view operator()(const Item &item) const noexcept;
	};

	struct ItemHash {
		[[gnu::pure]]
		size_t operator()(std::string_view item) const noexcept;
	};

	struct ItemEqual {
		[[gnu::pure]]
		bool operator()(std::string_view a, std::string_view b) const noexcept {
			return a == b;
		}
	};

	using Set =
		IntrusiveHashSet<Item, 4096,
				 IntrusiveHashSetOperators<Item, ItemGetKey,
							   ItemHash, ItemEqual>>;
	Set items;

public:
	Stock() noexcept;
	~Stock() noexcept;

	Stock(const Stock &) = delete;
	Stock &operator=(const Stock &) = delete;

	void FadeAll() noexcept;

	void Get(EventLoop &event_loop,
		 AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
		 std::string_view name,
		 SocketAddress bind_address,
		 SocketAddress address,
		 Event::Duration timeout,
		 const SocketFilterParams *filter_params,
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
