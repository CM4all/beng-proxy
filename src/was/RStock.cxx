// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "RStock.hxx"
#include "SConnection.hxx"
#include "was/async/Socket.hxx"
#include "was/async/MultiClient.hxx"
#include "cgi/Address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "AllocatorPtr.hxx"
#include "event/SocketEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/ConnectSocket.hxx"
#include "net/FormatAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "util/StringList.hxx"

#include <cassert>
#include <optional>

struct RemoteMultiWasParams {
	SocketAddress address;
	unsigned parallelism, concurrency;
};

class RemoteMultiWasConnection final
	: public StockItem, Was::MultiClientHandler
{
	std::optional<Was::MultiClient> client;

	bool busy = true;

public:
	RemoteMultiWasConnection(CreateStockItem c,
				 UniqueSocketDescriptor socket) noexcept
		:StockItem(c)
	{
		Was::MultiClientHandler &client_handler = *this;
		client.emplace(c.stock.GetEventLoop(), std::move(socket),
			       client_handler);
	}

	WasSocket Connect() {
		return client->Connect();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	void Disconnected() noexcept {
		client.reset();

		if (busy)
			InvokeBusyDisconnect();
		else
			InvokeIdleDisconnect();
	}

	/* virtual methods from class Was::MultiClientHandler */
	void OnMultiClientDisconnect() noexcept override {
		Disconnected();
	}

	void OnMultiClientError(std::exception_ptr error) noexcept override {
		(void)error; // TODO log error?
		Disconnected();
	}
};

bool
RemoteMultiWasConnection::Borrow() noexcept
{
	assert(!busy);
	busy = true;

	return true;
}

bool
RemoteMultiWasConnection::Release() noexcept
{
	assert(busy);
	busy = false;

	return true;
}

void
RemoteWasStock::MultiClientStockClass::Create(CreateStockItem c,
					      StockRequest request,
					      StockGetHandler &handler,
					      CancellablePointer &)
{
	const auto &params = *(const RemoteMultiWasParams *)request.get();

	auto *connection =
		new RemoteMultiWasConnection(c,
					     CreateConnectSocketNonBlock(params.address,
									 SOCK_SEQPACKET));
	connection->InvokeCreateSuccess(handler);
}

RemoteWasStock::RemoteWasStock(unsigned limit, [[maybe_unused]] unsigned max_idle,
			       EventLoop &event_loop) noexcept
	:multi_stock(event_loop, multi_client_stock_class,
		     limit,
		     // TODO max_idle,
		     *this) {}

StockOptions
RemoteWasStock::GetOptions(const void *request,
		      StockOptions o) const noexcept
{
	const auto &params = *(const RemoteMultiWasParams *)request;
	if (params.parallelism > 0)
		o.limit = params.parallelism;

	o.clear_interval = std::chrono::minutes{5};

	return o;
}

StockItem *
RemoteWasStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &multi_connection = (RemoteMultiWasConnection &)shared_item;

	auto *connection = new WasStockConnection(c, multi_connection.Connect());

#ifdef HAVE_URING
	if (uring_queue != nullptr)
		connection->EnableUring(*uring_queue);
#endif

	return connection;
}

void
RemoteWasStock::Get(AllocatorPtr alloc, SocketAddress address,
		   unsigned parallelism, unsigned concurrency,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<RemoteMultiWasParams>(alloc);
	r->address = address;
	r->parallelism = parallelism;
	r->concurrency = concurrency;

	char key[1024];
	if (!ToString(key, address))
		key[0] = 0;

	multi_stock.Get(StockKey{key}, std::move(r), concurrency, handler, cancel_ptr);
}
