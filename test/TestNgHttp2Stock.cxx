// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "nghttp2/Stock.hxx"
#include "nghttp2/Client.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/IPv4Address.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/SocketError.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <memory>

#include <sys/socket.h>

using std::string_view_literals::operator""sv;

namespace {

/**
 * A listening TCP socket on the loopback interface which never
 * accepts; connecting to it succeeds via the kernel's backlog, which
 * is all the #NgHttp2::Stock needs to construct a #ClientConnection.
 * TCP is used (instead of a local socket) because its connect does
 * not complete synchronously, so several requests can be queued on
 * one item.
 */
class Listener {
	UniqueSocketDescriptor socket;

	StaticSocketAddress address;

public:
	Listener() {
		if (!socket.CreateNonBlock(AF_INET, SOCK_STREAM, 0))
			throw MakeSocketError("Failed to create socket");

		if (!socket.Bind(IPv4Address{IPv4Address::Loopback(), 0}))
			throw MakeSocketError("Failed to bind");

		if (!socket.Listen(16))
			throw MakeSocketError("Failed to listen");

		address = socket.GetLocalAddress();
	}

	SocketAddress GetAddress() const noexcept {
		return address;
	}

	/**
	 * Stop listening, so that connecting to #address fails.
	 */
	void Close() noexcept {
		socket.Close();
	}
};

struct Recorder final : NgHttp2::StockGetHandler {
	EventLoop *event_loop = nullptr;

	NgHttp2::ClientConnection *connection = nullptr;

	std::exception_ptr error;

	bool alpn = false;

	CancellablePointer cancel_ptr;

	/**
	 * Cancel this #Recorder when our own handler is invoked; this
	 * simulates a sibling request which is aborted by the error
	 * cascade of another one.
	 */
	Recorder *cancel_on_callback = nullptr;

	bool IsPending() const noexcept {
		return connection == nullptr && !error && !alpn;
	}

	void Cancel() noexcept {
		cancel_ptr.Cancel();
	}

	/* virtual methods from class NgHttp2::StockGetHandler */
	void OnNgHttp2StockReady(NgHttp2::ClientConnection &_connection) noexcept override {
		cancel_ptr = nullptr;
		connection = &_connection;
		CancelSibling();
	}

	void OnNgHttp2StockAlpn(std::unique_ptr<FilteredSocket> &&) noexcept override {
		cancel_ptr = nullptr;
		alpn = true;
		CancelSibling();
	}

	void OnNgHttp2StockError(std::exception_ptr e) noexcept override {
		cancel_ptr = nullptr;
		error = std::move(e);
		CancelSibling();
	}

private:
	void CancelSibling() noexcept {
		if (cancel_on_callback != nullptr) {
			auto &sibling = *cancel_on_callback;
			cancel_on_callback = nullptr;
			sibling.Cancel();
		}

		if (event_loop != nullptr)
			/* stop the EventLoop; else it would wait for
			   the connection's idle timer */
			event_loop->Break();
	}
};

class NgHttp2StockTest : public ::testing::Test {
protected:
	TestInstance instance;

	NgHttp2::Stock stock;

	void Get(SocketAddress address, Recorder &recorder) noexcept {
		recorder.event_loop = &instance.event_loop;

		const AllocatorPtr alloc{*instance.root_pool};

		stock.Get(instance.event_loop, alloc, nullptr,
			  "test"sv,
			  SocketAddress::Null(), address,
			  std::chrono::seconds{30},
			  nullptr,
			  recorder, recorder.cancel_ptr);
	}
};

} // anonymous namespace

/**
 * A single request which succeeds.
 */
TEST_F(NgHttp2StockTest, Success)
{
	Listener listener;

	Recorder r;
	Get(listener.GetAddress(), r);

	instance.event_loop.Run();

	EXPECT_NE(r.connection, nullptr);
	EXPECT_FALSE(r.error);
}

/**
 * Nobody listens on the address, so the connect fails and the error
 * is reported to all waiting handlers.
 */
TEST_F(NgHttp2StockTest, ConnectError)
{
	Listener listener;
	const auto address = listener.GetAddress();
	listener.Close();

	Recorder a, b;
	Get(address, a);
	Get(address, b);

	instance.event_loop.Run();

	EXPECT_EQ(a.connection, nullptr);
	EXPECT_TRUE(a.error);
	EXPECT_EQ(b.connection, nullptr);
	EXPECT_TRUE(b.error);
}

/**
 * Cancelling the only request while the connect is still pending must
 * cancel the connect and delete the item.
 */
TEST_F(NgHttp2StockTest, CancelWhileConnecting)
{
	Listener listener;

	Recorder r;
	Get(listener.GetAddress(), r);

	ASSERT_TRUE(r.IsPending());
	r.Cancel();

	instance.event_loop.Run();

	EXPECT_TRUE(r.IsPending());
}

/**
 * While the item dispatches a successful connect, the first handler
 * cancels a sibling request.  The cancellation must not delete the
 * item under the running dispatch loop.
 */
TEST_F(NgHttp2StockTest, SiblingCancelOnSuccess)
{
	Listener listener;

	Recorder a, b;
	a.cancel_on_callback = &b;

	Get(listener.GetAddress(), a);
	Get(listener.GetAddress(), b);

	instance.event_loop.Run();

	EXPECT_NE(a.connection, nullptr);

	/* the sibling was canceled before its handler ran */
	EXPECT_TRUE(b.IsPending());
}

/**
 * Like SiblingCancelOnSuccess, but the connect fails; here the
 * dispatching code deletes the item after the loop, and the
 * cancellation must not delete it first.
 */
TEST_F(NgHttp2StockTest, SiblingCancelOnError)
{
	Listener listener;
	const auto address = listener.GetAddress();
	listener.Close();

	Recorder a, b;
	a.cancel_on_callback = &b;

	Get(address, a);
	Get(address, b);

	instance.event_loop.Run();

	EXPECT_TRUE(a.error);
	EXPECT_TRUE(b.IsPending());
}

/**
 * All requests are canceled while the successful connect is being
 * dispatched, leaving a connection which was never used.  The item
 * must remain usable for the next request.
 */
TEST_F(NgHttp2StockTest, ReuseAfterSiblingCancel)
{
	Listener listener;

	Recorder a, b;
	a.cancel_on_callback = &b;

	Get(listener.GetAddress(), a);
	Get(listener.GetAddress(), b);

	instance.event_loop.Run();

	ASSERT_NE(a.connection, nullptr);
	ASSERT_TRUE(b.IsPending());

	/* the next request reuses the very same connection */
	Recorder c;
	Get(listener.GetAddress(), c);

	EXPECT_EQ(c.connection, a.connection);
}

/**
 * FadeAll() deletes an idle item.
 */
TEST_F(NgHttp2StockTest, FadeAll)
{
	Listener listener;

	Recorder a;
	Get(listener.GetAddress(), a);

	instance.event_loop.Run();
	ASSERT_NE(a.connection, nullptr);

	stock.FadeAll();

	instance.event_loop.Run();

	/* the faded item is gone, so the next request has to
	   establish a new connection instead of being served
	   immediately */
	Recorder b;
	Get(listener.GetAddress(), b);

	EXPECT_TRUE(b.IsPending());

	instance.event_loop.Run();

	EXPECT_NE(b.connection, nullptr);
}
