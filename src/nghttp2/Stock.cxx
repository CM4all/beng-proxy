// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "Client.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/Connect.hxx"
#include "fs/Key.hxx"
#include "fs/Params.hxx"
#include "fs/Factory.hxx"
#include "ssl/AlpnCompare.hxx"
#include "ssl/Filter.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "net/SocketAddress.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/djb_hash.hxx"
#include "util/IntrusiveList.hxx"
#include "util/StringBuilder.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <string>

#include <string.h>

namespace NgHttp2 {

class Stock::Item final
	: public IntrusiveHashSetHook<IntrusiveHookMode::NORMAL>,
	  ConnectFilteredSocketHandler, ConnectionHandler
{
	Stock &stock;

	const std::string key;

	std::unique_ptr<ClientConnection> connection;

	struct GetRequest final
		: IntrusiveListHook<IntrusiveHookMode::NORMAL>,
		  Cancellable
	{
		Item &item;

		const StopwatchPtr stopwatch;

		StockGetHandler &handler;

		GetRequest(Item &_item,
			   const StopwatchPtr &parent_stopwatch,
			   StockGetHandler &_handler,
			   CancellablePointer &cancel_ptr) noexcept
			:item(_item),
			 stopwatch(parent_stopwatch, "connect"),
			 handler(_handler)
		{
			cancel_ptr = *this;
		}

		/* virtual methods from class Cancellable */
		void Cancel() noexcept override {
			stopwatch.RecordEvent("cancel");
			item.CancelGetRequest(*this);
		}
	};

	IntrusiveList<GetRequest> get_requests;

	CancellablePointer connect_cancel;

	CoarseTimerEvent idle_timer;

	bool go_away = false;

	bool alpn_failure = false;

public:
	template<typename K>
	Item(Stock &_stock, EventLoop &event_loop, K &&_key) noexcept;

	auto &GetEventLoop() const noexcept {
		return idle_timer.GetEventLoop();
	}

	const std::string &GetKey() const noexcept {
		return key;
	}

	bool IsIdle() const noexcept {
		return (!connection || connection->IsIdle()) &&
			get_requests.empty();
	}

	bool IsAvailable() const noexcept {
		return !go_away && (!connection || !connection->IsFull());
	}

	void Fade() noexcept {
		go_away = true;

		if (IsIdle())
			idle_timer.Schedule(std::chrono::seconds(0));
	}

	void Start(SocketAddress bind_address,
		   SocketAddress address,
		   Event::Duration timeout,
		   const SocketFilterParams *filter_params) noexcept;

	void FinishOne(std::unique_ptr<FilteredSocket> &&_socket,
		       StockGetHandler &handler) noexcept;

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
			request->stopwatch.RecordEvent("error");
			request->handler.OnNgHttp2StockError(e);
		});

		stock.DeleteItem(this);
	}

	/* virtual methods from class ConnectSocketHandler */
	void OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> socket) noexcept override;
	void OnConnectFilteredSocketError(std::exception_ptr e) noexcept override {
		AbortConnectError(std::move(e));
	}

	/* virtual methods from class ConnectionHandler */
	void OnNgHttp2ConnectionIdle() noexcept override;
	void OnNgHttp2ConnectionGoAway() noexcept override;
	void OnNgHttp2ConnectionError(std::exception_ptr e) noexcept override;
	void OnNgHttp2ConnectionClosed() noexcept override;
};

template<typename K>
Stock::Item::Item(Stock &_stock, EventLoop &event_loop, K &&_key) noexcept
	:stock(_stock), key(std::forward<K>(_key)),
	 idle_timer(event_loop, BIND_THIS_METHOD(OnIdleTimer))
{
}

void
Stock::Item::Start(SocketAddress bind_address,
		   SocketAddress address,
		   Event::Duration timeout,
		   const SocketFilterParams *filter_params) noexcept
{
	assert(!get_requests.empty());
	assert(!alpn_failure);

	ConnectFilteredSocket(GetEventLoop(),
			      get_requests.front().stopwatch,
			      false, bind_address, address, timeout,
			      filter_params != nullptr ? filter_params->CreateFactory() : nullptr,
			      *this, connect_cancel);
}

inline void
Stock::Item::FinishOne(std::unique_ptr<FilteredSocket> &&socket,
		       StockGetHandler &get_handler) noexcept
{
	assert(socket);
	assert(!connection);
	assert(get_requests.empty());

	NgHttp2::ConnectionHandler &nghttp2_handler = *this;
	connection = std::make_unique<ClientConnection>(std::move(socket),
							nghttp2_handler);

	idle_timer.Schedule(std::chrono::minutes(1));
	get_handler.OnNgHttp2StockReady(*connection);
}

void
Stock::Item::AddGetHandler(AllocatorPtr alloc,
			   const StopwatchPtr &parent_stopwatch,
			   StockGetHandler &handler,
			   CancellablePointer &cancel_ptr) noexcept
{
	if (connection) {
		idle_timer.Schedule(std::chrono::minutes(1));
		handler.OnNgHttp2StockReady(*connection);
	} else if (alpn_failure) {
		handler.OnNgHttp2StockAlpn(nullptr);
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
	assert(!connection);

	connect_cancel.Cancel();

	stock.DeleteItem(this);
}

void
Stock::Item::OnConnectFilteredSocket(std::unique_ptr<FilteredSocket> socket) noexcept
{
	assert(socket);
	assert(!get_requests.empty());
	assert(!connection);

	const auto *ssl_filter = ssl_filter_cast_from(socket->GetFilter());
	if (ssl_filter != nullptr && !IsAlpnHttp2(*ssl_filter)) {
		alpn_failure = true;

		auto _socket = std::move(socket);
		get_requests.clear_and_dispose([&_socket](GetRequest *request){
			request->stopwatch.RecordEvent("alpn");
			request->handler.OnNgHttp2StockAlpn(std::move(_socket));
		});

		/* this item stays in the map so it can serve
		   future requests quickly */
		return;
	}

	NgHttp2::ConnectionHandler &handler = *this;
	connection = std::make_unique<ClientConnection>(std::move(socket),
							handler);

	auto &c = *connection;
	get_requests.clear_and_dispose([&c](GetRequest *request){
		request->stopwatch.RecordEvent("success");
		request->handler.OnNgHttp2StockReady(c);
	});
}

void
Stock::Item::OnNgHttp2ConnectionIdle() noexcept
{
	assert(connection);
	assert(get_requests.empty());

	idle_timer.Schedule(go_away
			    ? std::chrono::minutes(0)
			    : std::chrono::minutes(1));
}

void
Stock::Item::OnNgHttp2ConnectionGoAway() noexcept
{
	assert(connection);

	Fade();
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
	assert(get_requests.empty());

	/* FadeAll() can schedule this call when there is no
	   connection, e.g. if alpn_failure is set */
	if (!connection || connection->IsIdle())
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

Stock::Stock() noexcept = default;

Stock::~Stock() noexcept
{
	items.clear_and_dispose(DeleteDisposer());
}

void
Stock::FadeAll() noexcept
{
	items.for_each([](auto &i){ i.Fade(); });
}

void
Stock::Get(EventLoop &event_loop,
	   AllocatorPtr alloc, const StopwatchPtr &parent_stopwatch,
	   const char *name,
	   SocketAddress bind_address,
	   SocketAddress address,
	   Event::Duration timeout,
	   const SocketFilterParams *filter_params,
	   StockGetHandler &handler,
	   CancellablePointer &cancel_ptr) noexcept
{
	char key_buffer[1024];
	try {
		StringBuilder b(key_buffer);
		MakeFilteredSocketStockKey(b, name, bind_address, address,
					   filter_params);
	} catch (StringBuilder::Overflow) {
		/* shouldn't happen */
		handler.OnNgHttp2StockError(std::current_exception());
		return;
	}

	const char *key = key_buffer;

	if (auto i = items.find_if(key,
				   [](const auto &j){return j.IsAvailable();});
	    i != items.end()) {
		i->AddGetHandler(alloc, parent_stopwatch,
				 handler, cancel_ptr);
		return;
	}

	auto *item = new Item(*this, event_loop, key);
	items.insert(*item);
	item->AddGetHandler(alloc, parent_stopwatch, handler, cancel_ptr);
	item->Start(bind_address, address, timeout, filter_params);
}

void
Stock::Add(EventLoop &event_loop,
	   const char *key,
	   std::unique_ptr<FilteredSocket> socket,
	   StockGetHandler &handler) noexcept
{
	auto *item = new Item(*this, event_loop, key);
	items.insert(*item);
	item->FinishOne(std::move(socket), handler);
}

inline void
Stock::DeleteItem(Item *item) noexcept
{
	items.erase(items.iterator_to(*item));
	delete item;
}

} // namespace NgHttp2
