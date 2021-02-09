/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "event/Chrono.hxx"

#include <exception>
#include <memory>

#include <boost/intrusive/unordered_set.hpp>

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

	using SetHook = boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>;

	using Set = boost::intrusive::unordered_multiset<Item,
							 boost::intrusive::base_hook<SetHook>,
							 boost::intrusive::hash<ItemHash>,
							 boost::intrusive::equal<ItemEqual>,
							 boost::intrusive::constant_time_size<false>>;
	Set items;

	static constexpr size_t N_BUCKETS = 251;
	Set::bucket_type buckets[N_BUCKETS];

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

private:
	void DeleteItem(Item *item) noexcept;
};

} // namespace NgHttp2
