/*
 * Copyright 2007-2019 Content Management AG
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

#include "Stock.hxx"
#include "Client.hxx"
#include "fs/Factory.hxx"
#include "event/TimerEvent.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "net/ToString.hxx"
#include "io/Logger.hxx"
#include "util/djbhash.h"
#include "util/StringBuffer.hxx"
#include "AllocatorPtr.hxx"

#include <string>

#include <string.h>

namespace NgHttp2 {

class Stock::Item final
	: public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
	  ConnectSocketHandler, ConnectionHandler
{
	Stock &stock;

	const std::string key;

	SocketFilterFactory *const filter_factory;

	std::unique_ptr<ClientConnection> connection;

	struct GetRequest final
		: boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
		  Cancellable {
		Item &item;

		StockGetHandler &handler;

		GetRequest(Item &_item,
			   const StopwatchPtr &, // TODO
			   StockGetHandler &_handler,
			   CancellablePointer &cancel_ptr) noexcept
			:item(_item), handler(_handler)
		{
			cancel_ptr = *this;
		}

		/* virtual methods from class Cancellable */
		void Cancel() noexcept override {
			item.CancelGetRequest(*this);
		}
	};

	boost::intrusive::list<GetRequest,
			       boost::intrusive::constant_time_size<false>> get_requests;

	CancellablePointer connect_cancel;
	ConnectSocket connect_operation;

	TimerEvent idle_timer;

	FdType fd_type;

public:
	template<typename K>
	Item(Stock &_stock, EventLoop &event_loop, K &&_key,
	     SocketFilterFactory *_filter_factory) noexcept;

	auto &GetEventLoop() const noexcept {
		return idle_timer.GetEventLoop();
	}

	const std::string &GetKey() const noexcept {
		return key;
	}

	void Start(SocketAddress bind_address,
		   SocketAddress address,
		   Event::Duration timeout) noexcept;

	void AddGetHandler(AllocatorPtr alloc,
			   const StopwatchPtr &parent_stopwatch,
			   StockGetHandler &handler,
			   CancellablePointer &cancel_ptr) noexcept;

private:
	void OnIdleTimer() noexcept;

	void CancelGetRequest(GetRequest &request) noexcept;
	void Cancel() noexcept;

	void AbortConnectError(std::exception_ptr e) noexcept {
		assert(!connection);
		assert(!get_requests.empty());

		get_requests.clear_and_dispose([&e](GetRequest *request){
			request->handler.OnNgHttp2StockError(e);
		});

		stock.DeleteItem(this);
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept override;
	void OnSocketConnectError(std::exception_ptr e) noexcept override {
		AbortConnectError(std::move(e));
	}

	/* virtual methods from class ConnectionHandler */
	void OnNgHttp2ConnectionIdle() noexcept override;
	void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept override;
	void OnNgHttp2ConnectionClosed() noexcept override;
};

template<typename K>
Stock::Item::Item(Stock &_stock, EventLoop &event_loop, K &&_key,
		  SocketFilterFactory *_filter_factory) noexcept
	:stock(_stock), key(std::forward<K>(_key)),
	 filter_factory(_filter_factory),
	 connect_operation(event_loop, *this),
	 idle_timer(event_loop, BIND_THIS_METHOD(OnIdleTimer))
{
}

void
Stock::Item::Start(SocketAddress bind_address,
		   SocketAddress address,
		   Event::Duration timeout) noexcept
{
	const int address_family = address.GetFamily();
	fd_type = address_family == AF_LOCAL
		? FD_SOCKET
		: FD_TCP;

	(void)bind_address; // TODO

	connect_operation.Connect(address, timeout);
}

void
Stock::Item::AddGetHandler(AllocatorPtr alloc,
			   const StopwatchPtr &parent_stopwatch,
			   StockGetHandler &handler,
			   CancellablePointer &cancel_ptr) noexcept
{
	if (connection) {
		assert(!connect_operation.IsPending());

		idle_timer.Schedule(std::chrono::minutes(1));
		handler.OnNgHttp2StockReady(*connection);
	} else {
		auto *request = alloc.New<GetRequest>(*this, parent_stopwatch,
						      handler, cancel_ptr);
		get_requests.push_back(*request);
	}
}

void
Stock::Item::CancelGetRequest(GetRequest &request) noexcept
{
	assert(!get_requests.empty());

	get_requests.erase_and_dispose(get_requests.iterator_to(request),
				       [](GetRequest *r) { r->~GetRequest(); });

	if (get_requests.empty())
		Cancel();
}

void
Stock::Item::Cancel() noexcept
{
	assert(get_requests.empty());

	if (connect_operation.IsPending()) {
		assert(!connection);

		connect_cancel.Cancel();
	} else {
		// TODO cancel SSL handshake?
	}

	stock.DeleteItem(this);
}

void
Stock::Item::OnSocketConnectSuccess(UniqueSocketDescriptor &&fd) noexcept
{
	assert(!get_requests.empty());
	assert(!connection);

	NgHttp2::ConnectionHandler &handler = *this;
	connection = std::make_unique<ClientConnection>(GetEventLoop(),
							std::move(fd), fd_type,
							filter_factory != nullptr
							? filter_factory->CreateFilter()
							: nullptr,
							handler);

	auto &c = *connection;
	get_requests.clear_and_dispose([&c](GetRequest *request){
		request->handler.OnNgHttp2StockReady(c);
	});
}

void
Stock::Item::OnNgHttp2ConnectionIdle() noexcept
{
	assert(connection);
	assert(get_requests.empty());

	idle_timer.Schedule(std::chrono::minutes(1));
}

void
Stock::Item::OnNgHttp2ConnectionError(std::exception_ptr e) noexcept
{
	assert(connection);
	assert(get_requests.empty());

	LogConcat(1, key.c_str(), e);

	stock.DeleteItem(this);
}

void
Stock::Item::OnNgHttp2ConnectionClosed() noexcept
{
	assert(connection);
	assert(get_requests.empty());

	stock.DeleteItem(this);
}

void
Stock::Item::OnIdleTimer() noexcept
{
	assert(connection);
	assert(get_requests.empty());

	if (connection->IsIdle())
		stock.DeleteItem(this);
	else
		idle_timer.Schedule(std::chrono::minutes(1));
}

inline size_t
Stock::ItemHash::operator()(const char *key) const noexcept
{
	return djb_hash_string(key);
}

inline size_t
Stock::ItemHash::operator()(const Item &item) const noexcept
{
	return djb_hash_string(item.GetKey().c_str());
}

inline bool
Stock::ItemEqual::operator()(const char *a, const Item &b) const noexcept
{
	return a == b.GetKey();
}

inline bool
Stock::ItemEqual::operator()(const Item &a, const Item &b) const noexcept
{
	return a.GetKey() == b.GetKey();
}

Stock::Stock() noexcept
	:items(Set::bucket_traits(buckets, N_BUCKETS))
{
}

Stock::~Stock() noexcept = default;

static StringBuffer<1024>
MakeKey(SocketAddress bind_address, SocketAddress address,
	SocketFilterFactory *filter_factory) noexcept
{
	StringBuffer<1024> buffer;
	char *p = buffer.data();
	const auto end = buffer + buffer.capacity();

	if (!bind_address.IsNull()) {
		if (ToString(p, end - p - 1, bind_address))
			p += strlen(p);

		*p++ = '>';
	}

	if (ToString(p, end - p, address))
		p += strlen(p);

	if (filter_factory != nullptr && p + 2 < end) {
		*p++ = '|';

		const char *id = filter_factory->GetFilterId();
		if (id != nullptr) {
			auto length = std::min<size_t>(strlen(id), end - p - 1);
			p = (char *)mempcpy(p, id, length);
		}
	}

	*p = 0;

	return buffer;
}

void
Stock::Get(EventLoop &event_loop,
	   AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
	   const char *name,
	   SocketAddress bind_address,
	   SocketAddress address,
	   Event::Duration timeout,
	   SocketFilterFactory *filter_factory,
	   StockGetHandler &handler,
	   CancellablePointer &cancel_ptr) noexcept
{
	const char *key;
	StringBuffer<1024> key_buffer;
	if (name != nullptr)
		key = name;
	else {
		key_buffer = MakeKey(bind_address, address, filter_factory);
		key = key_buffer.c_str();
	}

	Set::insert_commit_data hint;
	auto i = items.insert_check(key, ItemHash(), ItemEqual(), hint);
	Item *item;
	if (i.second) {
		item = new Item(*this, event_loop, key, filter_factory);
		items.insert_commit(*item, hint);
	} else
		item = &*i.first;

	item->AddGetHandler(alloc, parent_stopwatch, handler, cancel_ptr);

	if (i.second)
		item->Start(bind_address, address, timeout);
}

inline void
Stock::DeleteItem(Item *item) noexcept
{
	items.erase(items.iterator_to(*item));
	delete item;
}

} // namespace NgHttp2
