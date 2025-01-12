// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "Connection.hxx"
#include "Address.hxx"
#include "http/Client.hxx" // for class HttpClientError
#include "stock/Stock.hxx"
#include "stock/MultiStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "pool/tpool.hxx"
#include "pool/WithPoolDisposablePointer.hxx"
#include "AllocatorPtr.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "spawn/ListenChildStock.hxx"
#include "spawn/Prepared.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "io/Logger.hxx"
#include "util/Exception.hxx"
#include "util/StringList.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

class LhttpStock final : MultiStockClass, ListenChildStockClass {
	PoolPtr pool{pool_new_dummy(nullptr, "LhttpStock")};
	ChildStock child_stock;
	MultiStock mchild_stock;

public:
	LhttpStock(unsigned limit, unsigned max_idle,
		   EventLoop &event_loop, SpawnService &spawn_service,
		   ListenStreamStock *_listen_stream_stock,
		   Net::Log::Sink *log_sink,
		   const ChildErrorLogOptions &log_options) noexcept;

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
	std::size_t GetLimit(const void *request,
			     std::size_t _limit) const noexcept override;
	Event::Duration GetClearInterval(const void *request) const noexcept override;
	StockItem *Create(CreateStockItem c, StockItem &shared_item) override;

	/* virtual methods from class ChildStockClass */
	StockRequest PreserveRequest(StockRequest request) noexcept override;
	bool WantStderrPond(const void *info) const noexcept override;
	std::string_view GetChildTag(const void *info) const noexcept override;
	void PrepareChild(const void *info, PreparedChildProcess &p,
			  FdHolder &close_fds) override;

	/* virtual methods from class ChildStockMapClass */
	std::size_t GetChildLimit(const void *request,
				  std::size_t _limit) const noexcept override;
	Event::Duration GetChildClearInterval(const void *info) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	int GetChildSocketType(const void *info) const noexcept override;
	unsigned GetChildBacklog(const void *info) const noexcept override;
	void PrepareListenChild(const void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p,
				FdHolder &close_fds) override;
};

static const char *
lhttp_stock_key(struct pool *pool, const LhttpAddress *address) noexcept
{
	return address->GetServerId(AllocatorPtr(*pool));
}

/*
 * child_stock class
 *
 */

std::size_t
LhttpStock::GetLimit(const void *request,
		     std::size_t _limit) const noexcept
{
	const auto &address = *(const LhttpAddress *)request;

	if (address.parallelism > 0)
		return address.parallelism;

	return _limit;
}

Event::Duration
LhttpStock::GetClearInterval(const void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	return address.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(15)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

/* TODO: this method is unreachable we don't use ChildStockMap, but we
   must implemented it because ListenChildStockClass is based on
   ChildStockMapClass */
std::size_t
LhttpStock::GetChildLimit(const void *request,
			  std::size_t _limit) const noexcept
{
	return GetLimit(request, _limit);
}

/* TODO: this method is unreachable we don't use ChildStockMap, but we
   must implemented it because ListenChildStockClass is based on
   ChildStockMapClass */
Event::Duration
LhttpStock::GetChildClearInterval(const void *info) const noexcept
{
	return GetClearInterval(info);
}

StockRequest
LhttpStock::PreserveRequest(StockRequest request) noexcept
{
	const auto &src = *reinterpret_cast<const LhttpAddress *>(request.get());
	return WithPoolDisposablePointer<LhttpAddress>::New(pool_new_linear(pool, "LhttpAddress", 4096), src);
}

bool
LhttpStock::WantStderrPond(const void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;
	return address.options.stderr_pond;
}

int
LhttpStock::GetChildSocketType(const void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	int type = SOCK_STREAM;
	if (!address.blocking)
		type |= SOCK_NONBLOCK;

	return type;
}

unsigned
LhttpStock::GetChildBacklog(const void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	/* use the concurrency for the listener backlog to ensure that
	   we'll never get ECONNREFUSED/EAGAIN while the child process
	   initializes itself */
	/* use a factor of 2 because cancelled requests during child
	   process startup count towards the backlog */
	return address.concurrency * 2;
}

std::string_view
LhttpStock::GetChildTag(const void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	return address.options.tag;
}

void
LhttpStock::PrepareChild(const void *info, PreparedChildProcess &p,
			 FdHolder &close_fds)
{
	const auto &address = *(const LhttpAddress *)info;

	address.CopyTo(p, close_fds);
}

void
LhttpStock::PrepareListenChild(const void *, UniqueSocketDescriptor fd,
			       PreparedChildProcess &p,
			       FdHolder &close_fds)
{
	p.stdin_fd = close_fds.Insert(std::move(fd).MoveToFileDescriptor());
}

/*
 * stock class
 *
 */

StockItem *
LhttpStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &child = (ListenChildStockItem &)shared_item;

	try {
		return new LhttpConnection(c, child);
	} catch (...) {
		std::throw_with_nested(HttpClientError(HttpClientErrorCode::REFUSED,
						       FmtBuffer<256>("Failed to connect to LHTTP server {:?}",
								      c.GetStockName())));
	}
}


/*
 * interface
 *
 */

inline
LhttpStock::LhttpStock(unsigned limit, [[maybe_unused]] unsigned max_idle,
		       EventLoop &event_loop, SpawnService &spawn_service,
		       ListenStreamStock *_listen_stream_stock,
		       Net::Log::Sink *log_sink,
		       const ChildErrorLogOptions &log_options) noexcept
	:child_stock(spawn_service, _listen_stream_stock,
		     *this,
		     log_sink, log_options),
	 mchild_stock(event_loop, child_stock,
		      limit,
		      // TODO max_idle,
		      *this)
{
}

void
LhttpStock::FadeTag(std::string_view tag) noexcept
{
	mchild_stock.FadeIf([tag](const StockItem &_item){
		auto &item = (const ChildStockItem &)_item;
		return StringListContains(item.GetTag(), '\0',
					  tag);
	});
}

LhttpStock *
lhttp_stock_new(unsigned limit, unsigned max_idle,
		EventLoop &event_loop, SpawnService &spawn_service,
		ListenStreamStock *listen_stream_stock,
		Net::Log::Sink *log_sink,
		const ChildErrorLogOptions &log_options) noexcept
{
	return new LhttpStock(limit, max_idle, event_loop, spawn_service,
			      listen_stream_stock,
			      log_sink, log_options);
}

void
lhttp_stock_free(LhttpStock *ls) noexcept
{
	delete ls;
}

std::size_t
lhttp_stock_discard_some(LhttpStock &ls) noexcept
{
	return ls.DiscardSome();
}

void
lhttp_stock_fade_all(LhttpStock &ls) noexcept
{
	ls.FadeAll();
}

void
lhttp_stock_fade_tag(LhttpStock &ls, std::string_view tag) noexcept
{
	ls.FadeTag(tag);
}

inline void
LhttpStock::Get(const LhttpAddress &address,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;
	mchild_stock.Get(lhttp_stock_key(tpool, &address),
			 ToNopPointer(const_cast<LhttpAddress *>(&address)),
			 address.concurrency,
			 handler, cancel_ptr);
}

void
lhttp_stock_get(LhttpStock *lhttp_stock,
		const LhttpAddress *address,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	lhttp_stock->Get(*address, handler, cancel_ptr);
}
