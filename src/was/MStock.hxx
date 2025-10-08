// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "spawn/ChildStock.hxx"
#include "stock/MultiStock.hxx"
#include "pool/Ptr.hxx"
#include "io/uring/config.h" // for HAVE_URING

#include <span>

class AllocatorPtr;
struct ChildOptions;
class StockItem;
struct WasSocket;
class SocketDescriptor;
class EventLoop;
class SpawnService;

#ifdef HAVE_URING
namespace Uring { class Queue; }
#endif

class MultiWasStock final : MultiStockClass, ChildStockClass {
	PoolPtr pool;
	ChildStock child_stock;
	MultiStock mchild_stock;

#ifdef HAVE_URING
	Uring::Queue *uring_queue = nullptr;
#endif

public:
	MultiWasStock(unsigned limit, unsigned max_idle,
		      EventLoop &event_loop, SpawnService &spawn_service,
#ifdef HAVE_LIBSYSTEMD
		      CgroupMultiWatch *_cgroup_multi_watch,
#endif
		      Net::Log::Sink *log_sink,
		      const ChildErrorLogOptions &log_options) noexcept;

	auto &GetEventLoop() const noexcept {
		return mchild_stock.GetEventLoop();
	}

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &_uring_queue) noexcept {
		uring_queue = &_uring_queue;
	}
#endif

	std::size_t DiscardSome() noexcept {
		return mchild_stock.DiscardOldestIdle(64);
	}

	void FadeAll() noexcept {
		mchild_stock.FadeAll();
	}

	void FadeTag(std::string_view tag) noexcept;

	/**
	 * The resulting #StockItem will be a #WasStockConnection
	 * instance.
	 */
	void Get(AllocatorPtr alloc,
		 StockKey key,
		 const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism, unsigned concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class MultiStockClass */
	StockOptions GetOptions(const void *request,
				StockOptions o) const noexcept override;
	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;

	/* virtual methods from class ChildStockClass */
	StockRequest PreserveRequest(StockRequest request) noexcept override;
	bool WantStderrPond(const void *info) const noexcept override;
	std::string_view GetChildTag(const void *info) const noexcept override;
	std::unique_ptr<ChildStockItem> CreateChild(CreateStockItem c,
						    const void *info,
						    ChildStock &child_stock) override;
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;
};
