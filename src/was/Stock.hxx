// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "access_log/ChildErrorLogOptions.hxx"
#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "net/SocketDescriptor.hxx"

#include <span>
#include <string_view>

struct pool;
struct ChildOptions;
struct WasSocket;
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

	void FadeTag(std::string_view tag) noexcept;

	/**
	 * The resulting #StockItem will be a #WasStockConnection
	 * instance.
	 *
	 * @param args command-line arguments
	 */
	void Get(struct pool &pool,
		 const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism, bool disposable,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};
