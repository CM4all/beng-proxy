// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "access_log/ChildErrorLogOptions.hxx"
#include "stock/Class.hxx"
#include "stock/MapStock.hxx"
#include "stock/Options.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/uring/config.h" // for HAVE_URING

#include <span>
#include <string_view>

namespace Net::Log { class Sink; }
class AllocatorPtr;
struct ChildOptions;
struct WasSocket;
class SpawnService;
class ListenStreamStock;

#ifdef HAVE_URING
namespace Uring { class Queue; }
#endif

/**
 * Launch and manage WAS child processes.
 */
class WasStock final : StockClass {
	SpawnService &spawn_service;
	ListenStreamStock *const listen_stream_stock;
	Net::Log::Sink *const log_sink;
	const ChildErrorLogOptions log_options;

	class WasStockMap final : public StockMap {
	public:
		using StockMap::StockMap;

		/* virtual methods from class StockMap */
		StockOptions GetOptions(const void *request,
					StockOptions o) const noexcept override;
	};

	WasStockMap stock;

#ifdef HAVE_URING
	Uring::Queue *uring_queue = nullptr;
#endif

public:
	explicit WasStock(EventLoop &event_loop, SpawnService &_spawn_service,
			  ListenStreamStock *_listen_stream_stock,
			  Net::Log::Sink *_log_sink,
			  const ChildErrorLogOptions &_log_options,
			  StockOptions stock_options) noexcept
		:spawn_service(_spawn_service),
		 listen_stream_stock(_listen_stream_stock),
		 log_sink(_log_sink), log_options(_log_options),
		 stock(event_loop, *this, stock_options) {}

	auto &GetEventLoop() const noexcept {
		return stock.GetEventLoop();
	}

	void AddStats(StockStats &data) const noexcept {
		stock.AddStats(data);
	}

#ifdef HAVE_URING
	void EnableUring(Uring::Queue &_uring_queue) noexcept {
		uring_queue = &_uring_queue;
	}
#endif

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
	void Get(AllocatorPtr alloc,
		 StockKey key,
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
