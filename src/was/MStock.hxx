/*
 * Copyright 2007-2022 CM4all GmbH
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

	void DiscardSome() noexcept {
		/* first close idle connections, hopefully turning
		   child processes idle */
		mchild_stock.DiscardUnused();

		/* kill the oldest child process */
		child_stock.DiscardOldestIdle();
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
