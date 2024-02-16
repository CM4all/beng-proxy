// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "Error.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ListenChildStock.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
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

class FcgiStock final : StockClass, ListenChildStockClass {
	StockMap hstock;
	ChildStockMap child_stock;

public:
	FcgiStock(unsigned limit, unsigned max_idle,
		  EventLoop &event_loop, SpawnService &spawn_service,
		  SocketDescriptor _log_socket,
		  const ChildErrorLogOptions &_log_options) noexcept;

	~FcgiStock() noexcept {
		/* this one must be cleared before #child_stock; FadeAll()
		   calls ClearIdle(), so this method is the best match for
		   what we want to do (though a kludge) */
		hstock.FadeAll();
	}

	EventLoop &GetEventLoop() noexcept {
		return hstock.GetEventLoop();
	}

	StockItem *Get(const ChildOptions &options,
		       const char *executable_path,
		       std::span<const char *const> args,
		       unsigned parallelism);

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
	std::size_t GetChildLimit(const void *request,
				  std::size_t _limit) const noexcept override;
	Event::Duration GetChildClearInterval(const void *info) const noexcept override;
	bool WantStderrFd(void *info) const noexcept override;
	bool WantStderrPond(void *info) const noexcept override;

	unsigned GetChildBacklog(void *) const noexcept override {
		return 4;
	}

	std::string_view GetChildTag(void *info) const noexcept override;
	void PrepareChild(void *info, PreparedChildProcess &p) override;

	/* virtual methods from class ListenChildStockClass */
	void PrepareListenChild(void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p) override;
};

struct FcgiConnection final : StockItem {
	const LLogger logger;

	ListenChildStockItem *child = nullptr;

	UniqueSocketDescriptor fd;
	SocketEvent event;

	UniqueFileDescriptor stderr_fd;

	/**
	 * Is this a fresh connection to the FastCGI child process?
	 */
	bool fresh = true;

	/**
	 * Shall the FastCGI child process be killed?
	 */
	bool kill = false;

	/**
	 * Was the current request aborted by the fcgi_client caller?
	 */
	bool aborted = false;

	explicit FcgiConnection(EventLoop &event_loop, CreateStockItem c) noexcept
		:StockItem(c), logger(GetStockName()),
		 event(event_loop, BIND_THIS_METHOD(OnSocketEvent)) {}

	~FcgiConnection() noexcept override;

	[[gnu::pure]]
	std::string_view GetTag() const noexcept {
		assert(child != nullptr);

		return child->GetTag();
	}

	UniqueFileDescriptor GetStderr() const noexcept {
		assert(child != nullptr);

		return child->GetStderr();
	}

	void SetSite(const char *site) noexcept {
		assert(child != nullptr);

		child->SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		assert(child != nullptr);

		child->SetUri(uri);
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	void OnSocketEvent(unsigned events) noexcept;
};

/*
 * libevent callback
 *
 */

void
FcgiConnection::OnSocketEvent(unsigned) noexcept
{
	std::byte buffer[1];
	ssize_t nbytes = fd.ReadNoWait(buffer);
	if (nbytes < 0)
		logger(2, "error on idle FastCGI connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle FastCGI connection");

	InvokeIdleDisconnect();
}

/*
 * child_stock class
 *
 */

std::size_t
FcgiStock::GetChildLimit(const void *request,
			 std::size_t _limit) const noexcept
{
	const auto &params = *(const CgiChildParams *)request;
	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

Event::Duration
FcgiStock::GetChildClearInterval(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(10)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

bool
FcgiStock::WantStderrFd(void *) const noexcept
{
	return true;
}

bool
FcgiStock::WantStderrPond(void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;
	return params.options.stderr_pond;
}

std::string_view
FcgiStock::GetChildTag(void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.tag;
}

void
FcgiStock::PrepareChild(void *info, PreparedChildProcess &p)
{
	auto &params = *(CgiChildParams *)info;
	const ChildOptions &options = params.options;

	/* the FastCGI protocol defines a channel for stderr, so we could
	   close its "real" stderr here, but many FastCGI applications
	   don't use the FastCGI protocol to send error messages, so we
	   just keep it open */

	UniqueFileDescriptor null_fd;
	if (null_fd.Open("/dev/null", O_WRONLY))
		p.SetStdout(std::move(null_fd));

	p.Append(params.executable_path);
	for (auto i : params.args)
		p.Append(i);

	options.CopyTo(p);
}

void
FcgiStock::PrepareListenChild(void *, UniqueSocketDescriptor fd,
			      PreparedChildProcess &p)
{
	p.SetStdin(std::move(fd));
}

/*
 * stock class
 *
 */

void
FcgiStock::Create(CreateStockItem c, StockRequest request,
		  StockGetHandler &handler,
		  [[maybe_unused]] CancellablePointer &cancel_ptr)
{
	[[maybe_unused]] auto &params = *(CgiChildParams *)request.get();
	assert(params.executable_path != nullptr);

	auto *connection = new FcgiConnection(GetEventLoop(), c);

	const char *key = c.GetStockName();

	try {
		connection->child = (ListenChildStockItem *)
			child_stock.GetStockMap().GetNow(key,
							 std::move(request));
	} catch (...) {
		delete connection;
		std::throw_with_nested(FcgiClientError(FmtBuffer<256>("Failed to start FastCGI server '{}'",
								      key)));
	}

	try {
		connection->fd = connection->child->Connect();
	} catch (...) {
		connection->kill = true;
		delete connection;
		std::throw_with_nested(FcgiClientError(FmtBuffer<256>("Failed to connect to FastCGI server '{}'",
								      key)));
	}

	connection->event.Open(connection->fd);

	connection->InvokeCreateSuccess(handler);
}

bool
FcgiConnection::Borrow() noexcept
{
	/* check the connection status before using it, just in case the
	   FastCGI server has decided to close the connection before
	   fcgi_connection_event_callback() got invoked */
	std::byte buffer[1];
	ssize_t nbytes = fd.ReadNoWait(buffer);
	if (nbytes > 0) {
		logger(2, "unexpected data from idle FastCGI connection");
		return false;
	} else if (nbytes == 0) {
		/* connection closed (not worth a log message) */
		return false;
	} else if (errno != EAGAIN) {
		logger(2, "error on idle FastCGI connection: ", strerror(errno));
		return false;
	}

	event.Cancel();
	aborted = false;
	return true;
}

bool
FcgiConnection::Release() noexcept
{
	fresh = false;
	event.ScheduleRead();
	return true;
}

FcgiConnection::~FcgiConnection() noexcept
{
	if (fd.IsDefined()) {
		event.Cancel();
		fd.Close();
	}

	if (fresh && aborted)
		/* the fcgi_client caller has aborted the request before the
		   first response on a fresh connection was received: better
		   kill the child process, it may be failing on us
		   completely */
		kill = true;

	if (child != nullptr)
		child->Put(kill ? PutAction::DESTROY : PutAction::REUSE);
}


/*
 * interface
 *
 */

inline
FcgiStock::FcgiStock(unsigned limit, unsigned max_idle,
		     EventLoop &event_loop, SpawnService &spawn_service,
		     SocketDescriptor _log_socket,
		     const ChildErrorLogOptions &_log_options) noexcept
	:hstock(event_loop, *this, limit, max_idle,
		std::chrono::minutes(2)),
	 child_stock(event_loop, spawn_service,
		     nullptr, // TODO do we need ListenStreamSpawnStock here?
		     *this,
		     _log_socket, _log_options,
		     limit, max_idle) {}

void
FcgiStock::FadeTag(std::string_view tag) noexcept
{
	hstock.FadeIf([tag](const StockItem &item){
		const auto &connection = (const FcgiConnection &)item;
		return StringListContains(connection.GetTag(), '\0', tag);
	});

	child_stock.FadeTag(tag);
}

FcgiStock *
fcgi_stock_new(unsigned limit, unsigned max_idle,
	       EventLoop &event_loop, SpawnService &spawn_service,
	       SocketDescriptor log_socket,
	       const ChildErrorLogOptions &log_options) noexcept
{
	return new FcgiStock(limit, max_idle, event_loop, spawn_service,
			     log_socket, log_options);
}

void
fcgi_stock_free(FcgiStock *fcgi_stock) noexcept
{
	delete fcgi_stock;
}

void
fcgi_stock_fade_all(FcgiStock &fs) noexcept
{
	fs.FadeAll();
}

void
fcgi_stock_fade_tag(FcgiStock &fs, std::string_view tag) noexcept
{
	fs.FadeTag(tag);
}

inline StockItem *
FcgiStock::Get(const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism)
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<CgiChildParams>(*tpool, executable_path,
						      args, options,
						      parallelism, 0, false);
	const char *key = r->GetStockKey(*tpool);
	return hstock.GetNow(key, std::move(r));
}

StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock,
	       const ChildOptions &options,
	       const char *executable_path,
	       std::span<const char *const> args,
	       unsigned parallelism)
{
	return fcgi_stock->Get(options, executable_path, args,
			       parallelism);
}

int
fcgi_stock_item_get_domain([[maybe_unused]] const StockItem &item) noexcept
{
	return AF_LOCAL;
}

UniqueFileDescriptor
fcgi_stock_item_get_stderr(const StockItem &item) noexcept
{
	const auto &connection = (const FcgiConnection &)item;
	return connection.GetStderr();
}

void
fcgi_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &connection = (FcgiConnection &)item;
	connection.SetSite(site);
}

void
fcgi_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &connection = (FcgiConnection &)item;
	connection.SetUri(uri);
}

SocketDescriptor
fcgi_stock_item_get(const StockItem &item) noexcept
{
	const auto *connection = (const FcgiConnection *)&item;

	assert(connection->fd.IsDefined());

	return connection->fd;
}

void
fcgi_stock_aborted(StockItem &item) noexcept
{
	auto *connection = (FcgiConnection *)&item;

	connection->aborted = true;
}
