// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestPool.hxx"
#include "fs/Stock.hxx"
#include "fs/FilteredSocket.hxx"
#include "fs/Lease.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "memory/fb_pool.hxx"
#include "event/Loop.hxx"
#include "net/IPv4Address.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <optional>

using std::string_view_literals::operator""sv;

namespace {

static constexpr auto stock_name = "test"sv;
static constexpr IPv4Address stock_address{127, 0, 0, 1, 80};

struct Instance final {
	EventLoop event_loop;

	[[no_unique_address]]
	const ScopeFbPoolInit fb_pool_init;

	TestPool pool;

	FilteredSocketStock stock{event_loop, 0, 16};

	void Get(StockGetHandler &handler) noexcept {
		CancellablePointer cancel_ptr;
		stock.Get(AllocatorPtr{pool}, nullptr, stock_name, 0,
			  false, nullptr, stock_address,
			  std::chrono::seconds{30}, nullptr,
			  handler, cancel_ptr);
	}
};

struct GetItem final : StockGetHandler {
	StockItem *item = nullptr;

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &_item) noexcept override {
		item = &_item;
	}

	void OnStockItemError(std::exception_ptr) noexcept override {
		ADD_FAILURE();
	}
};

/**
 * Reads from a #FilteredSocketLease and releases the socket back to
 * the stock in OnBufferedData(); then the item is borrowed again and
 * destroyed before OnBufferedData() returns.
 */
class ReleaseClient final : Lease, BufferedSocketHandler {
	Instance &instance;

	StockItem &item;

	std::optional<FilteredSocketLease> socket;

public:
	ReleaseClient(Instance &_instance, StockItem &_item) noexcept
		:instance(_instance), item(_item),
		 socket(std::in_place, fs_stock_item_get(item),
			static_cast<Lease &>(*this),
			std::chrono::seconds{30},
			static_cast<BufferedSocketHandler &>(*this))
	{
		socket->ScheduleRead();
	}

private:
	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept override {
		return item.Put(action);
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override {
		socket->DisposeConsumed(socket->GetAvailable());
		socket->Release(false, PutAction::REUSE);
		socket.reset();

		GetItem again;
		instance.Get(again);
		EXPECT_EQ(again.item, &item);
		if (again.item != nullptr)
			again.item->Put(PutAction::DESTROY);

		instance.event_loop.Break();
		return BufferedResult::DESTROYED;
	}

	bool OnBufferedClosed() noexcept override {
		ADD_FAILURE();
		return true;
	}

	bool OnBufferedWrite() override {
		return true;
	}

	void OnBufferedError(std::exception_ptr) noexcept override {
		ADD_FAILURE();
	}
};

} // anonymous namespace

/**
 * Release a socket to the stock (with PutAction::REUSE) in
 * OnBufferedData(), and then borrow it and destroy it before
 * returning.
 */
TEST(FilteredSocketStock, DestroyAfterReleaseInDataHandler)
{
	Instance instance;

	auto [a, b] = CreateStreamSocketPairNonBlock();

	instance.stock.Add(StockKey{stock_name}, stock_address,
			   std::make_unique<FilteredSocket>(instance.event_loop,
							    std::move(a), FD_SOCKET,
							    SocketFilterPtr{}));

	GetItem get;
	instance.Get(get);
	ASSERT_NE(get.item, nullptr);

	ReleaseClient client{instance, *get.item};

	ASSERT_EQ(b.Write(AsBytes("foo"sv)), 3);

	instance.event_loop.Run();

	/* the socket was closed */
	std::byte buffer[16];
	EXPECT_EQ(b.Read(buffer), 0);
}
