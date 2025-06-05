// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Stock.hxx"
#include "Client.hxx"
#include "stock/Item.hxx"
#include "net/ConnectSocket.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "io/Logger.hxx"

#include <string.h>
#include <errno.h>

class TranslationStock::Connection final : public StockItem {
	UniqueSocketDescriptor s;

	SocketEvent event;

public:
	Connection(CreateStockItem c, UniqueSocketDescriptor &&_s) noexcept
		:StockItem(c), s(std::move(_s)),
		 event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback), s) {}

	SocketDescriptor GetSocket() const noexcept {
		return s;
	}

private:
	void EventCallback(unsigned) noexcept {
		std::byte buffer[1];
		ssize_t nbytes = s.Receive(buffer, MSG_DONTWAIT);
		if (nbytes < 0)
			LogConcat(2, "translation",
				  "error on idle translation server connection: ",
				  strerror(errno));
		else if (nbytes > 0)
			LogConcat(2, "translation",
				  "unexpected data in idle translation server connection");

		InvokeIdleDisconnect();
	}

public:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		return true;
	}
};

void
TranslationStock::Create(CreateStockItem c, StockRequest,
			 StockGetHandler &handler,
			 CancellablePointer &)
{
	auto *connection = new Connection(c, CreateConnectSocketNonBlock(address, SOCK_STREAM));
	connection->InvokeCreateSuccess(handler);
}

SocketDescriptor
TranslationStock::GetSocket(const StockItem &item) noexcept
{
	const auto &connection = static_cast<const Connection &>(item);
	return connection.GetSocket();
}
