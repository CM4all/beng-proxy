// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ListenChildStock.hxx"
#include "stock/Item.hxx"
#include "event/SocketEvent.hxx"
#include "event/DeferEvent.hxx"
#include "io/Logger.hxx"

class UniqueSocketDescriptor;

class FcgiStockConnection final : public StockItem {
	const LLogger logger;

	ListenChildStockItem &child;

	SocketEvent event;

	/**
	 * This postpones the ScheduleRead() call, just in case the
	 * connection gets borrowed immediately by the next waiter (in
	 * which case the deferred ScheduleRead() call is canceled).
	 * This reduces the number of epoll_ctl() system calls.
	 */
	DeferEvent defer_schedule_read;

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

	SocketDescriptor GetSocket() const noexcept {
		assert(event.IsDefined());
		return event.GetSocket();
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
	void DeferredScheduleRead() noexcept {
		event.ScheduleRead();
	}

	void Read() noexcept;
	void OnSocketEvent(unsigned events) noexcept;
};

void
fcgi_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
fcgi_stock_item_set_uri(StockItem &item, const char *uri) noexcept;

/**
 * Returns the socket descriptor of the specified stock item.
 */
SocketDescriptor
fcgi_stock_item_get(const StockItem &item) noexcept;

UniqueFileDescriptor
fcgi_stock_item_get_stderr(const StockItem &item) noexcept;

/**
 * Let the fcgi_stock know that the client is being aborted.  The
 * fcgi_stock may then figure out that the client process is faulty
 * and kill it at the next chance.  Note that this function will not
 * release the process - fcgi_stock_put() stil needs to be called
 * after this function.
 */
void
fcgi_stock_aborted(StockItem &item) noexcept;
