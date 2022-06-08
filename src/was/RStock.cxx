/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "net/ToString.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"
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
					     CreateConnectSocket(params.address,
								 SOCK_SEQPACKET));
	connection->InvokeCreateSuccess(handler);
}

RemoteWasStock::RemoteWasStock(unsigned limit, unsigned max_idle,
			       EventLoop &event_loop) noexcept
	:multi_stock(event_loop, multi_client_stock_class,
		     limit, max_idle, *this) {}

std::size_t
RemoteWasStock::GetLimit(const void *request,
			 std::size_t _limit) const noexcept
{
	const auto &params = *(const RemoteMultiWasParams *)request;

	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

StockItem *
RemoteWasStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &multi_connection = (RemoteMultiWasConnection &)shared_item;

	return new WasStockConnection(c, multi_connection.Connect());
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

	char buffer[1024];
	if (!ToString(buffer, sizeof(buffer), address))
		buffer[0] = 0;

	const char *key = alloc.Dup(buffer);

	multi_stock.Get(key, std::move(r), concurrency, handler, cancel_ptr);
}
