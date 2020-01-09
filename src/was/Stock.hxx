/*
 * Copyright 2007-2017 Content Management AG
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
#include "util/Compiler.h"

#include <stdint.h>

struct pool;
struct ChildOptions;
struct WasProcess;
template<typename T> struct ConstBuffer;
class SpawnService;
class WasChild;

class WasStock final : StockClass {
	SpawnService &spawn_service;
	const SocketDescriptor log_socket;
	const ChildErrorLogOptions log_options;
	StockMap stock;

public:
	explicit WasStock(EventLoop &event_loop, SpawnService &_spawn_service,
			  const SocketDescriptor _log_socket,
			  const ChildErrorLogOptions &_log_options,
			  unsigned limit, unsigned max_idle) noexcept
		:spawn_service(_spawn_service),
		 log_socket(_log_socket), log_options(_log_options),
		 stock(event_loop, *this, limit, max_idle) {}

	StockMap &GetStock() noexcept {
		return stock;
	}

	void FadeTag(const char *tag) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};

/**
 * Launch and manage WAS child processes.
 */
StockMap *
was_stock_new(unsigned limit, unsigned max_idle,
	      EventLoop &event_loop, SpawnService &spawn_service,
	      SocketDescriptor log_socket,
	      const ChildErrorLogOptions &log_options) noexcept;

void
was_stock_free(StockMap *stock) noexcept;

void
was_stock_fade_tag(StockMap &s, const char *tag) noexcept;

/**
 * @param args command-line arguments
 */
void
was_stock_get(StockMap *hstock, struct pool *pool,
	      const ChildOptions &options,
	      const char *executable_path,
	      ConstBuffer<const char *> args,
	      StockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept;

void
was_stock_item_set_site(StockItem &item, const char *site) noexcept;

void
was_stock_item_set_uri(StockItem &item, const char *uri) noexcept;

/**
 * Returns the socket descriptor of the specified stock item.
 */
gcc_pure
const WasProcess &
was_stock_item_get(const StockItem &item) noexcept;

/**
 * Set the "stopping" flag.  Call this after sending
 * #WAS_COMMAND_STOP, before calling hstock_put().  This will make the
 * stock wait for #WAS_COMMAND_PREMATURE.
 */
void
was_stock_item_stop(StockItem &item, uint64_t received) noexcept;
