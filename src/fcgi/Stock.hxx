// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool/Ptr.hxx"
#include "spawn/ListenChildStock.hxx"
#include "stock/MultiStock.hxx"

#include <span>
#include <string_view>

class CancellablePointer;
struct ChildErrorLogOptions;
class StockItem;
class StockGetHandler;
class FcgiStock;
struct ChildOptions;
class EventLoop;
class SpawnService;
class ListenStreamStock;
class UniqueFileDescriptor;
class UniqueSocketDescriptor;

class FcgiStock final : MultiStockClass, ListenChildStockClass {
	PoolPtr pool;
	ChildStock child_stock;
	MultiStock mchild_stock;

public:
	FcgiStock(unsigned limit, unsigned max_idle,
		  EventLoop &event_loop, SpawnService &spawn_service,
#ifdef HAVE_LIBSYSTEMD
		  CgroupMultiWatch *_cgroup_multi_watch,
#endif
		  ListenStreamStock *listen_stream_stock,
		  Net::Log::Sink *log_sink,
		  const ChildErrorLogOptions &_log_options) noexcept;

	~FcgiStock() noexcept;

	EventLoop &GetEventLoop() const noexcept {
		return mchild_stock.GetEventLoop();
	}

	/**
	 * @param args command-line arguments
	 */
	void Get(StockKey key, const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism, unsigned concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	void FadeAll() noexcept {
		mchild_stock.FadeAll();
	}

	void FadeTag(std::string_view tag) noexcept;

private:
	/* virtual methods from class MultiStockClass */
	std::size_t GetLimit(const void *request,
			     std::size_t _limit) const noexcept override;
	Event::Duration GetClearInterval(const void *request) const noexcept override;
	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;

	/* virtual methods from class ChildStockClass */
	StockRequest PreserveRequest(StockRequest request) noexcept override;
	bool WantStderrFd(const void *info) const noexcept override;
	bool WantStderrPond(const void *info) const noexcept override;
	std::string_view GetChildTag(const void *info) const noexcept override;
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;

	/* virtual methods from class ChildStockMapClass */
	StockOptions GetChildOptions(const void *request,
				     StockOptions o) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	unsigned GetChildBacklog(const void *info) const noexcept override;
	void PrepareListenChild(const void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p,
				FdHolder &close_fds) override;
};
