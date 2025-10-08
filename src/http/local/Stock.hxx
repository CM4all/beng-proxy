// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool/Ptr.hxx"
#include "spawn/ListenChildStock.hxx"
#include "stock/MultiStock.hxx"

#include <cstddef>
#include <string_view>

namespace Net::Log { class Sink; }
struct ChildErrorLogOptions;
class LhttpStock;
class StockGetHandler;
class CancellablePointer;
struct LhttpAddress;
class EventLoop;
class SpawnService;
class ListenStreamStock;

class LhttpStock final : MultiStockClass, ListenChildStockClass {
	PoolPtr pool;
	ChildStock child_stock;
	MultiStock mchild_stock;

public:
	LhttpStock(unsigned limit, unsigned max_idle,
		   EventLoop &event_loop, SpawnService &spawn_service,
#ifdef HAVE_LIBSYSTEMD
		   CgroupMultiWatch *_cgroup_multi_watch,
#endif
		   ListenStreamStock *_listen_stream_stock,
		   Net::Log::Sink *log_sink,
		   const ChildErrorLogOptions &log_options) noexcept;
	~LhttpStock() noexcept;

	void AddStats(StockStats &data) const noexcept {
		mchild_stock.AddStats(data);
	}

	/**
	 * Discard one or more processes to free some memory.
	 */
	std::size_t DiscardSome() noexcept {
		return mchild_stock.DiscardOldestIdle(64);
	}

	void FadeAll() noexcept {
		mchild_stock.FadeAll();
	}

	void FadeTag(std::string_view tag) noexcept;

	void Get(const LhttpAddress &address,
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
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;

	/* virtual methods from class ChildStockMapClass */
	StockOptions GetChildOptions(const void *request,
				     StockOptions o) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	int GetChildSocketType(const void *info) const noexcept override;
	unsigned GetChildBacklog(const void *info) const noexcept override;
	void PrepareListenChild(const void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p,
				FdHolder &close_fds) override;
};
