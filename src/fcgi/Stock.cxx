// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "SConnection.hxx"
#include "Error.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/GetHandler.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/WithPoolDisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/StringList.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef __linux
#include <sched.h>
#endif

class FcgiStock::CreateRequest final : StockGetHandler, Cancellable {
	CreateStockItem create;
	StockGetHandler &handler;
	CancellablePointer cancel_ptr;

public:
	CreateRequest(const CreateStockItem &_create,
		      StockGetHandler &_handler) noexcept
		:create(_create), handler(_handler) {}

	void Start(StockMap &child_stock_map,
		   StockRequest &&request,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		child_stock_map.Get(create.GetStockName(),
				    std::move(request),
				    *this, cancel_ptr);
	}

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		delete this;
	}
};

/*
 * child_stock class
 *
 */

std::size_t
FcgiStock::GetLimit(const void *request,
		    std::size_t _limit) const noexcept
{
	const auto &params = *(const CgiChildParams *)request;
	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

Event::Duration
FcgiStock::GetClearInterval(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(10)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

/* TODO: this method is unreachable we don't use ChildStockMap, but we
   must implemented it because ListenChildStockClass is based on
   ChildStockMapClass */
std::size_t
FcgiStock::GetChildLimit(const void *request,
			 std::size_t _limit) const noexcept
{
	return GetLimit(request, _limit);
}

/* TODO: this method is unreachable we don't use ChildStockMap, but we
   must implemented it because ListenChildStockClass is based on
   ChildStockMapClass */
Event::Duration
FcgiStock::GetChildClearInterval(const void *info) const noexcept
{
	return GetClearInterval(info);
}

StockRequest
FcgiStock::PreserveRequest(StockRequest request) noexcept
{
	const auto &src = *reinterpret_cast<const CgiChildParams *>(request.get());
	return WithPoolDisposablePointer<CgiChildParams>::New(pool_new_linear(pool, "CgiChildParams", 4096), src);
}

bool
FcgiStock::WantStderrFd(const void *) const noexcept
{
	return true;
}

bool
FcgiStock::WantStderrPond(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;
	return params.options.stderr_pond;
}

unsigned
FcgiStock::GetChildBacklog(const void *info) const noexcept
{
	const auto &address = *(const CgiChildParams *)info;

	/* use the concurrency for the listener backlog to ensure that
	   we'll never get ECONNREFUSED/EAGAIN while the child process
	   initializes itself */
	/* use a factor of 2 because cancelled requests during child
	   process startup count towards the backlog */
	return address.concurrency * 2;
}

std::string_view
FcgiStock::GetChildTag(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.tag;
}

void
FcgiStock::PrepareChild(const void *info, PreparedChildProcess &p,
			FdHolder &close_fds)
{
	const auto &params = *(const CgiChildParams *)info;
	const ChildOptions &options = params.options;

	/* the FastCGI protocol defines a channel for stderr, so we could
	   close its "real" stderr here, but many FastCGI applications
	   don't use the FastCGI protocol to send error messages, so we
	   just keep it open */

	if (UniqueFileDescriptor null_fd;
	    null_fd.Open("/dev/null", O_WRONLY))
		p.stdout_fd = close_fds.Insert(std::move(null_fd));

	p.Append(params.executable_path);
	for (auto i : params.args)
		p.Append(i);

	options.CopyTo(p, close_fds);
}

void
FcgiStock::PrepareListenChild(const void *, UniqueSocketDescriptor fd,
			      PreparedChildProcess &p,
			      FdHolder &close_fds)
{
	p.stdin_fd = close_fds.Insert(std::move(fd).MoveToFileDescriptor());
}

void
FcgiStock::CreateRequest::OnStockItemReady(StockItem &item) noexcept
{
	auto &child = static_cast<ListenChildStockItem &>(item);

	try {
		auto *connection = new FcgiStockConnection(create, child, child.Connect());
		connection->InvokeCreateSuccess(handler);
	} catch (...) {
		child.Put(PutAction::DESTROY);

		try {
			std::throw_with_nested(FcgiClientError(FcgiClientErrorCode::REFUSED,
							       FmtBuffer<256>("Failed to connect to FastCGI server {:?}",
									      create.GetStockName())));
		} catch (...) {
			create.InvokeCreateError(handler, std::current_exception());
		}
	}

	delete this;
}

void
FcgiStock::CreateRequest::OnStockItemError(std::exception_ptr error) noexcept
{
	create.InvokeCreateError(handler, std::move(error));
	delete this;
}

/*
 * stock class
 *
 */

StockItem *
FcgiStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &child = (ListenChildStockItem &)shared_item;

	try {
		return new FcgiStockConnection(c, child, child.Connect());
	} catch (...) {
		std::throw_with_nested(FcgiClientError(FcgiClientErrorCode::REFUSED,
						       FmtBuffer<256>("Failed to connect to FastCGI server {:?}",
								      c.GetStockName())));
	}
}

/*
 * interface
 *
 */

FcgiStock::FcgiStock(unsigned limit, [[maybe_unused]] unsigned max_idle,
		     EventLoop &event_loop, SpawnService &spawn_service,
		     ListenStreamStock *listen_stream_stock,
		     Net::Log::Sink *log_sink,
		     const ChildErrorLogOptions &_log_options) noexcept
	:pool(pool_new_dummy(nullptr, "FcgiStock")),
	 child_stock(spawn_service, listen_stream_stock,
		     *this,
		     log_sink, _log_options),
	 mchild_stock(event_loop, child_stock,
		      limit,
		      // TODO max_idle,
		      *this)
{
}

FcgiStock::~FcgiStock() noexcept = default;

void
FcgiStock::FadeTag(std::string_view tag) noexcept
{
	mchild_stock.FadeIf([tag](const StockItem &_item){
		auto &item = (const ChildStockItem &)_item;
		return StringListContains(item.GetTag(), '\0', tag);
	});
}

void
FcgiStock::Get(const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism, unsigned concurrency,
	       StockGetHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept
{
	if (concurrency <= 1)
		/* no concurrency by default */
		concurrency = 1;

	const TempPoolLease tpool;

	auto r = ToDeletePointer(new CgiChildParams(executable_path,
						    args, options,
						    parallelism, concurrency, false));
	const char *key = r->GetStockKey(*tpool);
	mchild_stock.Get(key, std::move(r),
			 concurrency,
			 handler, cancel_ptr);
}
