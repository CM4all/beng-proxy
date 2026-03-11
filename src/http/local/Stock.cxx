// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Stock.hxx"
#include "Connection.hxx"
#include "cgi/ChildParams.hxx"
#include "http/Client.hxx" // for class HttpClientError
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "pool/tpool.hxx"
#include "pool/WithPoolDisposablePointer.hxx"
#include "AllocatorPtr.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "spawn/ChildOptions.hxx"
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

/*
 * child_stock class
 *
 */

StockOptions
LhttpStock::GetOptions(const void *request,
		      StockOptions o) const noexcept
{
	const auto &address = *reinterpret_cast<const CgiChildParams *>(request);

	if (address.parallelism > 0)
		o.limit = address.parallelism;

	if (address.options.ns.mount.pivot_root != nullptr)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		o.clear_interval /= 3;

	return o;
}

/* TODO: this method is unreachable we don't use ChildStockMap, but we
   must implemented it because ListenChildStockClass is based on
   ChildStockMapClass */
StockOptions
LhttpStock::GetChildOptions(const void *request,
			    StockOptions o) const noexcept
{
	return GetOptions(request, o);
}

StockRequest
LhttpStock::PreserveRequest(StockRequest request) noexcept
{
	const auto &src = *reinterpret_cast<const CgiChildParams *>(request.get());
	return WithPoolDisposablePointer<CgiChildParams>::New(pool_new_linear(pool, "LhttpAddress", 4096), src);
}

bool
LhttpStock::WantStderrPond(const void *info) const noexcept
{
	const auto &address = *reinterpret_cast<const CgiChildParams *>(info);
	return address.options.stderr_pond;
}

int
LhttpStock::GetChildSocketType(const void *info) const noexcept
{
	const auto &address = *reinterpret_cast<const CgiChildParams *>(info);

	int type = SOCK_STREAM;
	if (!address.blocking)
		type |= SOCK_NONBLOCK;

	return type;
}

unsigned
LhttpStock::GetChildBacklog(const void *info) const noexcept
{
	const auto &address = *reinterpret_cast<const CgiChildParams *>(info);

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
	const auto &address = *reinterpret_cast<const CgiChildParams *>(info);

	return address.options.tag;
}

void
LhttpStock::PrepareChild(const void *info, PreparedChildProcess &p,
			 FdHolder &close_fds)
{
	const auto &address = *reinterpret_cast<const CgiChildParams *>(info);

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
								      c.GetStockNameView())));
	}
}


/*
 * interface
 *
 */

LhttpStock::LhttpStock(EventLoop &event_loop, SpawnService &spawn_service,
#ifdef HAVE_LIBSYSTEMD
		       CgroupMultiWatch *_cgroup_multi_watch,
#endif
		       ListenStreamStock *_listen_stream_stock,
		       StockOptions stock_options,
		       Net::Log::Sink *log_sink,
		       const ChildErrorLogOptions &log_options) noexcept
	:pool(pool_new_dummy(nullptr, "LhttpStock")),
	 child_stock(spawn_service,
#ifdef HAVE_LIBSYSTEMD
		     _cgroup_multi_watch,
#endif
		     _listen_stream_stock,
		     *this,
		     log_sink, log_options),
	 mchild_stock(event_loop, child_stock,
		      stock_options,
		      *this)
{
}

LhttpStock::~LhttpStock() noexcept = default;

void
LhttpStock::FadeTag(std::string_view tag) noexcept
{
	mchild_stock.FadeIf([tag](const StockItem &_item){
		auto &item = (const ChildStockItem &)_item;
		return StringListContains(item.GetTag(), '\0',
					  tag);
	});
}

void
LhttpStock::Get(StockKey key, const CgiChildParams &params,
		StockGetHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;
	mchild_stock.Get(key, ToNopPointer(&params),
			 params.concurrency,
			 handler, cancel_ptr);
}
