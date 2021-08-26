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

#pragma once

#include "ChildStock.hxx"
#include "spawn/ExitListener.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "stock/Item.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/StringView.hxx"

#include <string>

/**
 * A process managed by #ChildStock.
 */
class ChildStockItem
	: public StockItem,
	  public ChildStockItemHook,
	  ExitListener
{
	ChildStock &child_stock;

	const std::string tag;

	ChildErrorLog log;

	UniqueFileDescriptor stderr_fd;

	int pid = -1;

	bool busy = true;

public:
	ChildStockItem(CreateStockItem c,
		       ChildStock &_child_stock,
		       std::string_view _tag) noexcept
		:StockItem(c),
		 child_stock(_child_stock),
		 tag(_tag) {}

	~ChildStockItem() noexcept override;

	auto &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	/**
	 * Throws on error.
	 */
	void Spawn(ChildStockClass &cls, void *info,
		   SocketDescriptor log_socket,
		   const ChildErrorLogOptions &log_options);

	[[gnu::pure]]
	StringView GetTag() const noexcept {
		return tag.empty() ? nullptr : StringView{std::string_view{tag}};
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
