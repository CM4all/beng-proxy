// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "spawn/ChildStock.hxx"
#include "stock/MultiStock.hxx"

#include <span>

class AllocatorPtr;
struct ChildOptions;
class StockItem;
struct WasSocket;
class SocketDescriptor;
class EventLoop;
class SpawnService;

class MultiWasStock final : MultiStockClass, ChildStockClass {
	ChildStock child_stock;
	MultiStock mchild_stock;

public:
	MultiWasStock(unsigned limit, unsigned max_idle,
		      EventLoop &event_loop, SpawnService &spawn_service,
		      SocketDescriptor log_socket,
		      const ChildErrorLogOptions &log_options) noexcept;

	bool DiscardOldestIdle() noexcept {
		/* kill the oldest child process if there is one */
		return child_stock.DiscardOldestIdle() ||
			/* first close idle connections, hopefully
			   turning child processes idle */
			mchild_stock.DiscardOldestIdle() ||
			/* try again */
			child_stock.DiscardOldestIdle();
	}

	void DiscardSome() noexcept {
		for (unsigned i = 0; i < 64; ++i)
			if (!DiscardOldestIdle())
				break;
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
		 const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism, unsigned concurrency,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class MultiStockClass */
	std::size_t GetLimit(const void *request,
			     std::size_t _limit) const noexcept override;
	Event::Duration GetClearInterval(const void *info) const noexcept override;
	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;

	/* virtual methods from class ChildStockClass */
	bool WantStderrPond(void *info) const noexcept override;
	std::string_view GetChildTag(void *info) const noexcept override;
	std::unique_ptr<ChildStockItem> CreateChild(CreateStockItem c,
						    void *info,
						    ChildStock &child_stock) override;
	void PrepareChild(void *info, PreparedChildProcess &p) override;
};
