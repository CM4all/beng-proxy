// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Item.hxx"
#include "event/SocketEvent.hxx"
#include "io/FdType.hxx"
#include "io/Logger.hxx"

class ListenChildStockItem;

class LhttpConnection final
	: LoggerDomainFactory, public StockItem
{
	LazyDomainLogger logger;

	ListenChildStockItem &child;

	SocketEvent event;

public:
	/**
	 * Throws on connect error.
	 */
	explicit LhttpConnection(CreateStockItem c,
				 ListenChildStockItem &_child);
	~LhttpConnection() noexcept override;

	SocketDescriptor GetSocket() const noexcept {
		assert(event.IsDefined());
		return event.GetSocket();
	}

	void AbandonSocket() noexcept {
		assert(event.IsDefined());
		assert(event.GetScheduledFlags() == 0);

		event.Abandon();
	}

	[[gnu::pure]]
	std::string_view GetTag() const noexcept;

	void SetSite(const char *site) noexcept;
	void SetUri(const char *uri) noexcept;

private:
	void Read() noexcept;
	void EventCallback(unsigned events) noexcept;

	/* virtual methods from LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override {
		return GetStockName();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;
};

/**
 * Returns the socket descriptor of the specified stock item.
 */
[[gnu::pure]]
SocketDescriptor
lhttp_stock_item_get_socket(const StockItem &item) noexcept;

/**
 * Abandon the socket.  Invoke this if the socket returned by
 * lhttp_stock_item_get_socket() has been closed by its caller.
 */
void
lhttp_stock_item_abandon_socket(StockItem &item) noexcept;

[[gnu::pure]]
FdType
lhttp_stock_item_get_type(const StockItem &item) noexcept;

void
lhttp_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
lhttp_stock_item_set_uri(StockItem &item, const char *uri) noexcept;
