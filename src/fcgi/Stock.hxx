// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/Ptr.hxx"
#include "spawn/ListenChildStock.hxx"
#include "stock/MapStock.hxx"

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

class FcgiStock final : StockClass, ListenChildStockClass {
	PoolPtr pool;
	StockMap hstock;
	ChildStockMap child_stock;

	class CreateRequest;

public:
	FcgiStock(unsigned limit, unsigned max_idle,
		  EventLoop &event_loop, SpawnService &spawn_service,
		  ListenStreamStock *listen_stream_stock,
		  Net::Log::Sink *log_sink,
		  const ChildErrorLogOptions &_log_options) noexcept;

	~FcgiStock() noexcept {
		/* this one must be cleared before #child_stock; FadeAll()
		   calls ClearIdle(), so this method is the best match for
		   what we want to do (though a kludge) */
		hstock.FadeAll();
	}

	EventLoop &GetEventLoop() const noexcept {
		return hstock.GetEventLoop();
	}

	/**
	 * @param args command-line arguments
	 */
	void Get(const ChildOptions &options,
		 const char *executable_path,
		 std::span<const char *const> args,
		 unsigned parallelism,
		 StockGetHandler &handler,
		 CancellablePointer &cancel_ptr) noexcept;

	void FadeAll() noexcept {
		hstock.FadeAll();
		child_stock.GetStockMap().FadeAll();
	}

	void FadeTag(std::string_view tag) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;

	/* virtual methods from class ChildStockClass */
	StockRequest PreserveRequest(StockRequest request) noexcept override;

	bool WantStderrFd(const void *info) const noexcept override;
	bool WantStderrPond(const void *info) const noexcept override;

	unsigned GetChildBacklog(const void *) const noexcept override {
		return 4;
	}

	std::string_view GetChildTag(const void *info) const noexcept override;
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;

	/* virtual methods from class ChildStockMapClass */
	std::size_t GetChildLimit(const void *request,
				  std::size_t _limit) const noexcept override;
	Event::Duration GetChildClearInterval(const void *info) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	void PrepareListenChild(const void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p,
				FdHolder &close_fds) override;
};
