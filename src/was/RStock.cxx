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
#include "lease.hxx"
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
	unsigned concurrency;
};

class RemoteMultiWasConnection final
	: public StockItem, Was::MultiClientHandler
{
	Was::MultiClient client;

	bool busy = true;

public:
	RemoteMultiWasConnection(CreateStockItem c,
				 UniqueSocketDescriptor socket) noexcept
		:StockItem(c),
		 client(c.stock.GetEventLoop(), std::move(socket), *this)
	{}

	WasSocket Connect() {
		return client.Connect();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	void Disconnected() noexcept {
		fade = true;

		if (!busy)
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

class RemoteWasConnection final
	: public WasStockConnection, Cancellable, StockGetHandler
{
	RemoteMultiWasConnection *multi_connection = nullptr;

	CancellablePointer get_cancel_ptr;

	LeasePtr lease_ref;

public:
	using WasStockConnection::WasStockConnection;

	~RemoteWasConnection() noexcept override;

	void Connect(MultiStock &multi_stock,
		     StockRequest &&request,
		     unsigned concurrency,
		     CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		InvokeCreateAborted();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;
};

RemoteWasConnection::~RemoteWasConnection() noexcept
{
	if (multi_connection != nullptr)
		lease_ref.Release(true);
	else if (get_cancel_ptr)
		get_cancel_ptr.CancelAndClear();
}

inline void
RemoteWasConnection::Connect(MultiStock &multi_stock,
			     StockRequest &&request,
			     unsigned concurrency,
			     CancellablePointer &caller_cancel_ptr) noexcept
{
	caller_cancel_ptr = *this;

	multi_stock.Get(GetStockName(), std::move(request), concurrency,
			lease_ref, *this, get_cancel_ptr);
}

void
RemoteWasConnection::OnStockItemReady(StockItem &item) noexcept
{
	get_cancel_ptr = nullptr;

	multi_connection = (RemoteMultiWasConnection *)&item;

	try {
		Open(multi_connection->Connect());
	} catch (...) {
		InvokeCreateError(NestException(std::current_exception(),
						FormatRuntimeError("Failed to connect to RemoteWAS server '%s'",
								   GetStockName())));
		return;
	}

	InvokeCreateSuccess();
}

void
RemoteWasConnection::OnStockItemError(std::exception_ptr error) noexcept
{
	get_cancel_ptr = nullptr;

	InvokeCreateError(NestException(std::move(error),
					FormatRuntimeError("Failed to connect to RemoteWAS server %s",
							   GetStockName())));
}

void
RemoteWasStock::MultiClientStockClass::Create(CreateStockItem c,
					      StockRequest request,
					      CancellablePointer &)
{
	const auto &params = *(const RemoteMultiWasParams *)request.get();

	auto *connection =
		new RemoteMultiWasConnection(c,
					     CreateConnectSocket(params.address,
								 SOCK_SEQPACKET));
	connection->InvokeCreateSuccess();
}

RemoteWasStock::RemoteWasStock(unsigned limit, unsigned max_idle,
			       EventLoop &event_loop) noexcept
	:multi_client_stock(event_loop,
			    multi_client_stock_class,
			    limit, max_idle,
			    std::chrono::minutes{5}),
	 multi_stock(multi_client_stock),
	 connection_stock(event_loop, *this, 0, max_idle,
			  std::chrono::minutes{2}) {}

void
RemoteWasStock::Create(CreateStockItem c, StockRequest request,
		       CancellablePointer &cancel_ptr)
{
	const auto &params = *(const RemoteMultiWasParams *)request.get();

	auto *connection = new RemoteWasConnection(c);
	connection->Connect(multi_stock, std::move(request),
			    params.concurrency,
			    cancel_ptr);
}

void
RemoteWasStock::Get(AllocatorPtr alloc, SocketAddress address,
		   unsigned concurrency,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<RemoteMultiWasParams>(alloc);
	r->address = address;
	r->concurrency = concurrency;

	char buffer[1024];
	if (!ToString(buffer, sizeof(buffer), address))
		buffer[0] = 0;

	const char *key = alloc.Dup(buffer);

	GetConnectionStock().Get(key, std::move(r), handler, cancel_ptr);
}
