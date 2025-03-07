// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "access_log/ChildErrorLogOptions.hxx"
#include "util/IntrusiveList.hxx"
#include "config.h" // for HAVE_LIBSYSTEMD

#include <memory>
#include <string_view>

namespace Net::Log { class Sink; }
struct PreparedChildProcess;
class FileDescriptor;
class UniqueFileDescriptor;
class FdHolder;
class EventLoop;
class SpawnService;
class ListenStreamStock;
class ChildStock;
class ChildStockItem;
class CgroupMultiWatch;
class CgroupWatchPtr;
struct StringWithHash;

/*
 * Launch processes and connect a stream socket to them.
 */
class ChildStockClass {
public:
	/**
	 * Transform a #StockRequest so that it can be preserved
         * across asynchronous calls.
	 */
	virtual StockRequest PreserveRequest(StockRequest request) noexcept = 0;

	/**
	 * Implement this if you need to use
	 * ChildStockItem::GetStderr().  This will keep a copy of the
	 * stderr file descriptor, and if necessary, will ask the
	 * spawner to return it through a socket pair.
	 */
	virtual bool WantStderrFd(const void *) const noexcept {
		return false;
	}

	/**
	 * Obtain the value of #ChildOptions::stderr_pond.
	 */
	virtual bool WantStderrPond(const void *) const noexcept = 0;

	[[gnu::pure]]
	virtual std::string_view GetChildTag(const void *info) const noexcept;

	virtual std::unique_ptr<ChildStockItem> CreateChild(CreateStockItem c,
							    const void *info,
							    ChildStock &child_stock);

	/**
	 * Throws on error.
	 */
	virtual void PrepareChild(const void *info, PreparedChildProcess &p,
				  FdHolder &close_fds) = 0;
};

class ChildStockMapClass : public ChildStockClass {
public:
	virtual std::size_t GetChildLimit(const void *request,
					  std::size_t _limit) const noexcept = 0;

	virtual Event::Duration GetChildClearInterval(const void *info) const noexcept = 0;
};

/**
 * A stock which spawns and manages reusable child processes
 * (e.g. FastCGI servers).  The meaning of the "info" pointer and key
 * strings are defined by the given #ChildStockClass.
 */
class ChildStock final : public StockClass {
	SpawnService &spawn_service;

#ifdef HAVE_LIBSYSTEMD
	CgroupMultiWatch *const cgroup_multi_watch;
#endif

	ListenStreamStock *const listen_stream_stock;

	ChildStockClass &cls;

	Net::Log::Sink *const log_sink;

	const ChildErrorLogOptions log_options;

	class QueueItem;

	/**
	 * A list of idle items, the most recently used at the end.
	 * This is used by DiscardOldestIdle().
	 */
	IntrusiveList<ChildStockItem> idle;

public:
	ChildStock(SpawnService &_spawn_service,
#ifdef HAVE_LIBSYSTEMD
		   CgroupMultiWatch *_cgroup_multi_watch,
#endif
		   ListenStreamStock *_listen_stream_stock,
		   ChildStockClass &_cls,
		   Net::Log::Sink *_log_sink,
		   const ChildErrorLogOptions &_log_options) noexcept;
	~ChildStock() noexcept;

	auto &GetSpawnService() const noexcept {
		return spawn_service;
	}

	auto *GetListenStreamStock() const noexcept {
		return listen_stream_stock;
	}

	auto &GetClass() const noexcept {
		return cls;
	}

	Net::Log::Sink *GetLogSink() const noexcept {
		return log_sink;
	}

	const auto &GetLogOptions() const noexcept {
		return log_options;
	}

#ifdef HAVE_LIBSYSTEMD
	CgroupWatchPtr GetCgroupWatch(StringWithHash name) const noexcept;
#endif

	/**
	 * For internal use only.
	 */
	void AddIdle(ChildStockItem &item) noexcept;

	/**
	 * Kill the oldest idle child process across all stocks.
	 *
	 * @return false if no child process is idle
	 */
	bool DiscardOldestIdle() noexcept;

private:
	void DoSpawn(CreateStockItem c, StockRequest request,
		     StockGetHandler &handler,
		     CancellablePointer &caller_cancel_ptr) noexcept;

	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
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
		ChildStockMapClass &ccls;

	public:
		MyStockMap(EventLoop &_event_loop, StockClass &_cls,
			   ChildStockMapClass &_ccls,
			   unsigned _limit, unsigned _max_idle) noexcept
			:StockMap(_event_loop, _cls, _limit, _max_idle,
				  Event::Duration::zero()),
			 ccls(_ccls) {}

	protected:
		/* virtual method from class StockMap */
		std::size_t GetLimit(const void *request,
				     std::size_t _limit) const noexcept override {
			return ccls.GetChildLimit(request, _limit);
		}

		Event::Duration GetClearInterval(const void *info) const noexcept override {
			return ccls.GetChildClearInterval(info);
		}
	};

	MyStockMap map;

public:
	ChildStockMap(EventLoop &event_loop, SpawnService &_spawn_service,
#ifdef HAVE_LIBSYSTEMD
		      CgroupMultiWatch *_cgroup_multi_watch,
#endif
		      ListenStreamStock *_listen_stream_stock,
		      ChildStockMapClass &_cls,
		      Net::Log::Sink *_log_sink,
		      const ChildErrorLogOptions &_log_options,
		      unsigned _limit, unsigned _max_idle) noexcept;

	StockMap &GetStockMap() noexcept {
		return map;
	}

	Net::Log::Sink *GetLogSink() const noexcept {
		return cls.GetLogSink();
	}

	const auto &GetLogOptions() const noexcept {
		return cls.GetLogOptions();
	}

	/**
	 * "Fade" all child processes with the given tag.
	 */
	void FadeTag(std::string_view tag) noexcept;

	/**
	 * Kill the oldest idle child process across all stocks.
	 */
	void DiscardOldestIdle() noexcept {
		cls.DiscardOldestIdle();
	}
};
