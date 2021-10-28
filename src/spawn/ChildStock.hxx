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

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "access_log/ChildErrorLogOptions.hxx"
#include "net/SocketDescriptor.hxx"

#include <memory>
#include <string_view>

struct PreparedChildProcess;
class UniqueFileDescriptor;
class UniqueSocketDescriptor;
class EventLoop;
class SpawnService;
class ChildStock;
class ChildStockItem;

/*
 * Launch processes and connect a stream socket to them.
 */
class ChildStockClass {
public:
	virtual Event::Duration GetChildClearInterval(void *info) const noexcept = 0;

	/**
	 * Implement this if you wish the child process to return the
         * stderr file descriptor it opened to the returned socket.
         * This allows calling ChildStockItem::GetStderr().
	 */
	virtual bool WantReturnStderr(void *) const noexcept {
		return false;
	}

	/**
	 * Obtain the value of #ChildOptions::stderr_pond.
	 */
	virtual bool WantStderrPond(void *) const noexcept = 0;

	[[gnu::pure]]
	virtual StringView GetChildTag(void *info) const noexcept;

	virtual std::unique_ptr<ChildStockItem> CreateChild(CreateStockItem c,
							    void *info,
							    ChildStock &child_stock);

	/**
	 * Throws on error.
	 */
	virtual void PrepareChild(void *info, PreparedChildProcess &p) = 0;
};

using ChildStockItemHook =
	boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>,
					 boost::intrusive::tag<ChildStockClass>>;

/**
 * A stock which spawns and manages reusable child processes
 * (e.g. FastCGI servers).  The meaning of the "info" pointer and key
 * strings are defined by the given #ChildStockClass.
 */
class ChildStock final : public StockClass {
	SpawnService &spawn_service;

	ChildStockClass &cls;

	const SocketDescriptor log_socket;

	const ChildErrorLogOptions log_options;

	/**
	 * A list of idle items, the most recently used at the end.
	 * This is used by DiscardOldestIdle().
	 */
	boost::intrusive::list<ChildStockItem,
			       boost::intrusive::base_hook<ChildStockItemHook>,
			       boost::intrusive::constant_time_size<false>> idle;

public:
	ChildStock(SpawnService &_spawn_service,
		   ChildStockClass &_cls,
		   SocketDescriptor _log_socket,
		   const ChildErrorLogOptions &_log_options) noexcept;
	~ChildStock() noexcept;

	auto &GetSpawnService() const noexcept {
		return spawn_service;
	}

	auto &GetClass() const noexcept {
		return cls;
	}

	SocketDescriptor GetLogSocket() const noexcept {
		return log_socket;
	}

	const auto &GetLogOptions() const noexcept {
		return log_options;
	}

	/**
	 * For internal use only.
	 */
	void AddIdle(ChildStockItem &item) noexcept;

	/**
	 * Kill the oldest idle child process across all stocks.
	 */
	void DiscardOldestIdle() noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};

/**
 * A stock which spawns and manages reusable child processes
 * (e.g. FastCGI servers).  It is based on #StockMap.  The meaning of
 * the "info" pointer and key strings are defined by the given
 * #ChildStockClass.
 */
class ChildStockMap final {
	ChildStock cls;

	class MyStockMap final : public StockMap {
		ChildStockClass &ccls;

	public:
		MyStockMap(EventLoop &_event_loop, StockClass &_cls,
			   ChildStockClass &_ccls,
			   unsigned _limit, unsigned _max_idle) noexcept
			:StockMap(_event_loop, _cls, _limit, _max_idle,
				  Event::Duration::zero()),
			 ccls(_ccls) {}

	protected:
		Event::Duration GetClearInterval(void *info) const noexcept override {
			return ccls.GetChildClearInterval(info);
		}
	};

	MyStockMap map;

public:
	ChildStockMap(EventLoop &event_loop, SpawnService &_spawn_service,
		      ChildStockClass &_cls,
		      SocketDescriptor _log_socket,
		      const ChildErrorLogOptions &_log_options,
		      unsigned _limit, unsigned _max_idle) noexcept;

	StockMap &GetStockMap() noexcept {
		return map;
	}

	auto GetLogSocket() const noexcept {
		return cls.GetLogSocket();
	}

	const auto &GetLogOptions() const noexcept {
		return cls.GetLogOptions();
	}

	/**
	 * "Fade" all child processes with the given tag.
	 */
	void FadeTag(StringView tag) noexcept;

	/**
	 * Kill the oldest idle child process across all stocks.
	 */
	void DiscardOldestIdle() noexcept {
		cls.DiscardOldestIdle();
	}
};
