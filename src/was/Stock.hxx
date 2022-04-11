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

#include "access_log/ChildErrorLogOptions.hxx"
#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "net/SocketDescriptor.hxx"

#include <stdint.h>

struct pool;
struct StringView;
struct ChildOptions;
struct WasSocket;
template<typename T> struct ConstBuffer;
class SpawnService;

/**
 * Launch and manage WAS child processes.
 */
class WasStock final : StockClass {
	SpawnService &spawn_service;
	const SocketDescriptor log_socket;
	const ChildErrorLogOptions log_options;

	class WasStockMap final : public StockMap {
	public:
		using StockMap::StockMap;

		/* virtual methods from class StockMap */
		std::size_t GetLimit(const void *request,
				     std::size_t _limit) const noexcept override;
	};

	WasStockMap stock;

public:
	explicit WasStock(EventLoop &event_loop, SpawnService &_spawn_service,
			  const SocketDescriptor _log_socket,
			  const ChildErrorLogOptions &_log_options,
			  unsigned limit, unsigned max_idle) noexcept
		:spawn_service(_spawn_service),
		 log_socket(_log_socket), log_options(_log_options),
		 stock(event_loop, *this, limit, max_idle,
		       std::chrono::minutes(10)) {}

	void FadeAll() noexcept {
		stock.FadeAll();
	}

	void FadeTag(StringView tag) noexcept;

	/**
	 * The resulting #StockItem will be a #WasStockConnection
	 * instance.
	 *
	 * @param args command-line arguments
	 */
	void Get(struct pool &pool,
		 const ChildOptions &options,
		 const char *executable_path,
		 ConstBuffer<const char *> args,
		 unsigned parallelism,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};
