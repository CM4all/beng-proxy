// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ExitListener.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "stock/AbstractStock.hxx"
#include "stock/Item.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <memory>
#include <string>

class ChildProcessHandle;
class ChildStock;
class ChildStockClass;

/**
 * A process managed by #ChildStock.
 */
class ChildStockItem
	: public StockItem,
	  public AutoUnlinkIntrusiveListHook,
	  ExitListener
{
	ChildStock &child_stock;

	const std::string tag;

	ChildErrorLog log;

	UniqueFileDescriptor stderr_fd;

	std::unique_ptr<ChildProcessHandle> handle;

	/**
	 * A lease obtained from #ListenStreamSpawnStock.
	 */
	SharedLease listen_stream_lease;

	bool busy = true;

public:
	ChildStockItem(CreateStockItem c,
		       ChildStock &_child_stock,
		       std::string_view _tag) noexcept;

	~ChildStockItem() noexcept override;

	auto &GetEventLoop() const noexcept {
		return GetStock().GetEventLoop();
	}

	/**
	 * Throws on error.
	 */
	void Spawn(ChildStockClass &cls, void *info,
		   SocketDescriptor log_socket,
		   const ChildErrorLogOptions &log_options);

	[[gnu::pure]]
	std::string_view GetTag() const noexcept {
		if (tag.empty())
			return {};

		return tag;
	}

	[[gnu::pure]]
	bool IsTag(std::string_view _tag) const noexcept;

	UniqueFileDescriptor GetStderr() const noexcept;

	void SetSite(const char *site) noexcept {
		log.SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		log.SetUri(uri);
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;


private:
	/* virtual methods from class ExitListener */
	void OnChildProcessExit(int status) noexcept override;

protected:
	/**
	 * Call when this child process has disconnected.  This
	 * #StockItem will not be used again.
	 */
	void Disconnected() noexcept;

	/**
	 * Throws on error.
	 */
	virtual void Prepare(ChildStockClass &cls, void *info,
			     PreparedChildProcess &p);
};
