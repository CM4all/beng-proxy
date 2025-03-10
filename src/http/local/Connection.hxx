// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Item.hxx"
#include "fs/FilteredSocket.hxx"
#include "io/FdType.hxx"
#include "io/Logger.hxx"

#include <cassert>

class ListenChildStockItem;

class LhttpConnection final
	: public StockItem, BufferedSocketHandler
{
	LLogger logger;

	ListenChildStockItem &child;

	FilteredSocket socket;

public:
	/**
	 * Throws on connect error.
	 */
	explicit LhttpConnection(CreateStockItem c,
				 ListenChildStockItem &_child);
	~LhttpConnection() noexcept override;

	FilteredSocket &GetSocket() noexcept {
		assert(socket.IsValid());
		assert(socket.IsConnected());

		return socket;
	}

	[[gnu::pure]]
	std::string_view GetTag() const noexcept;

	void SetSite(const char *site) noexcept;
	void SetUri(const char *uri) noexcept;

private:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	[[noreturn]] bool OnBufferedWrite() override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

/**
 * Returns the socket descriptor of the specified stock item.
 */
[[gnu::pure]]
inline FilteredSocket &
lhttp_stock_item_get_socket(StockItem &item) noexcept
{
	auto &connection = static_cast<LhttpConnection &>(item);

	return connection.GetSocket();
}

inline void
lhttp_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &connection = static_cast<LhttpConnection &>(item);

	connection.SetSite(site);
}

inline void
lhttp_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &connection = static_cast<LhttpConnection &>(item);

	connection.SetUri(uri);
}
