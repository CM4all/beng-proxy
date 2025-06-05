// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "spawn/ListenChildStock.hxx"
#include "stock/Item.hxx"
#include "event/net/BufferedSocket.hxx"
#include "io/Logger.hxx"

class UniqueSocketDescriptor;

class FcgiStockConnection final : public StockItem, BufferedSocketHandler {
	const LLogger logger;

	ListenChildStockItem &child;

	BufferedSocket socket;

	/**
	 * Is this a fresh connection to the FastCGI child process?
	 */
	bool fresh = true;

public:
	explicit FcgiStockConnection(CreateStockItem c, ListenChildStockItem &_child,
				     UniqueSocketDescriptor &&socket) noexcept;

	~FcgiStockConnection() noexcept override;

	[[gnu::pure]]
	std::string_view GetTag() const noexcept {
		return child.GetTag();
	}

	BufferedSocket &GetSocket() noexcept {
		assert(socket.IsConnected());

		return socket;
	}

	UniqueFileDescriptor GetStderr() const noexcept {
		return child.GetStderr();
	}

	void SetSite(const char *site) noexcept {
		child.SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		child.SetUri(uri);
	}

	void SetAborted() noexcept;

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	[[noreturn]] bool OnBufferedWrite() override;
	void OnBufferedError(std::exception_ptr e) noexcept override;
};

static inline void
fcgi_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &connection = (FcgiStockConnection &)item;
	connection.SetSite(site);
}

static inline void
fcgi_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &connection = (FcgiStockConnection &)item;
	connection.SetUri(uri);
}

/**
 * Returns the socket of the specified stock item.
 */
static inline BufferedSocket &
fcgi_stock_item_get(StockItem &item) noexcept
{
	auto &connection = (FcgiStockConnection &)item;

	return connection.GetSocket();
}

static inline UniqueFileDescriptor
fcgi_stock_item_get_stderr(const StockItem &item) noexcept
{
	const auto &connection = (const FcgiStockConnection &)item;
	return connection.GetStderr();
}

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
static inline void
fcgi_stock_aborted(StockItem &item) noexcept
{
	auto *connection = (FcgiStockConnection *)&item;

	connection->SetAborted();
}
