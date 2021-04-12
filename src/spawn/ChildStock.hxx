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
#include "io/FdType.hxx"
#include "net/SocketDescriptor.hxx"

struct PreparedChildProcess;
class UniqueFileDescriptor;
class UniqueSocketDescriptor;
class EventLoop;
class SpawnService;
class ChildStockItem;

/*
 * Launch processes and connect a stream socket to them.
 */
class ChildStockClass {
public:
	virtual Event::Duration GetChildClearInterval(void *info) const noexcept = 0;

	/**
	 * Determine the socket type for the given child process.  The
	 * default is SOCK_STREAM.  This method may also be used to add
	 * the SOCK_NONBLOCK flag.  SOCK_CLOEXEC should not be used; it is
	 * added automatically.
	 *
	 * @param info an opaque pointer describing the process to be
	 * spawned
	 */
	virtual int GetChildSocketType(void *info) const noexcept;

	virtual unsigned GetChildBacklog(void *info) const noexcept;

	/**
	 * Implement this if you wish the child process to return the
         * stderr file descriptor it opened to the returned socket.
         * This allows calling child_stock_item_get_stderr().
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

	/**
	 * Throws std::runtime_error on error.
	 */
	virtual void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
				  PreparedChildProcess &p) = 0;
};

using ChildStockItemHook =
	boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>,
					 boost::intrusive::tag<ChildStockClass>>;

/**
 * A stock which spawns and manages reusable child processes
 * (e.g. FastCGI servers).  It is based on #StockMap.  The meaning of
 * the "info" pointer and key strings are defined by the given
 * #ChildStockClass.
 */
class ChildStock final : StockClass {
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

	/**
	 * A list of idle items, the most recently used at the end.
	 * This is used by DiscardOldestIdle().
	 */
	boost::intrusive::list<ChildStockItem,
			       boost::intrusive::base_hook<ChildStockItemHook>,
			       boost::intrusive::constant_time_size<false>> idle;

	MyStockMap map;

	SpawnService &spawn_service;
	ChildStockClass &cls;

	const unsigned backlog;

	const SocketDescriptor log_socket;

	const ChildErrorLogOptions log_options;

public:
	ChildStock(EventLoop &event_loop, SpawnService &_spawn_service,
		   ChildStockClass &_cls,
		   unsigned _backlog,
		   SocketDescriptor _log_socket,
		   const ChildErrorLogOptions &_log_options,
		   unsigned _limit, unsigned _max_idle) noexcept;

	~ChildStock() noexcept;

	StockMap &GetStockMap() noexcept {
		return map;
	}

	SocketDescriptor GetLogSocket() const noexcept {
		return log_socket;
	}

	const auto &GetLogOptions() const noexcept {
		return log_options;
	}

	/**
	 * "Fade" all child processes with the given tag.
	 */
	void FadeTag(StringView tag) noexcept;

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
 * Connect a socket to the given child process.  The socket must be
 * closed before the #stock_item is returned.
 *
 * Throws std::runtime_error on error.
 *
 * @return a socket descriptor
 */
UniqueSocketDescriptor
child_stock_item_connect(StockItem &item);

constexpr FdType
child_stock_item_get_type(const StockItem &) noexcept
{
	return FdType::FD_SOCKET;
}

[[gnu::pure]]
StringView
child_stock_item_get_tag(const StockItem &item);

UniqueFileDescriptor
child_stock_item_get_stderr(const StockItem &item) noexcept;

void
child_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
child_stock_item_set_uri(StockItem &item, const char *uri) noexcept;
