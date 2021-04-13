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

#include "Stock.hxx"
#include "Error.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "spawn/ChildStock.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "pool/StringBuilder.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringFormat.hxx"
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

class FcgiStock final : StockClass, ChildStockClass {
	StockMap hstock;
	ChildStock child_stock;

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

	SocketDescriptor GetLogSocket() const noexcept {
		return child_stock.GetLogSocket();
	}

	const auto &GetLogOptions() const noexcept {
		return child_stock.GetLogOptions();
	}

	StockItem *Get(const ChildOptions &options,
		       const char *executable_path,
		       ConstBuffer<const char *> args);

	void FadeAll() noexcept {
		hstock.FadeAll();
		child_stock.GetStockMap().FadeAll();
	}

	void FadeTag(StringView tag) noexcept;

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;

	/* virtual methods from class ChildStockClass */
	Event::Duration GetChildClearInterval(void *info) const noexcept override;
	bool WantReturnStderr(void *info) const noexcept override;
	bool WantStderrPond(void *info) const noexcept override;
	StringView GetChildTag(void *info) const noexcept override;
	void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
			  PreparedChildProcess &p) override;
};

struct FcgiChildParams {
	const char *executable_path;

	ConstBuffer<const char *> args;

	const ChildOptions &options;

	FcgiChildParams(const char *_executable_path,
			ConstBuffer<const char *> _args,
			const ChildOptions &_options) noexcept
		:executable_path(_executable_path), args(_args),
		 options(_options) {}

	const char *GetStockKey(struct pool &pool) const noexcept;
};

struct FcgiConnection final : StockItem {
	const LLogger logger;

	StockItem *child = nullptr;

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
	StringView GetTag() const noexcept {
		assert(child != nullptr);

		return child_stock_item_get_tag(*child);
	}

	UniqueFileDescriptor GetStderr() const noexcept {
		assert(child != nullptr);

		return child_stock_item_get_stderr(*child);
	}

	void SetSite(const char *site) noexcept {
		child_stock_item_set_site(*child, site);
	}

	void SetUri(const char *uri) noexcept {
		child_stock_item_set_uri(*child, uri);
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override;
	bool Release() noexcept override;

private:
	void OnSocketEvent(unsigned events) noexcept;
};

const char *
FcgiChildParams::GetStockKey(struct pool &pool) const noexcept
{
	PoolStringBuilder<256> b;
	b.push_back(executable_path);

	for (auto i : args) {
		b.push_back(" ");
		b.push_back(i);
	}

	for (auto i : options.env) {
		b.push_back("$");
		b.push_back(i);
	}

	char options_buffer[16384];
	b.emplace_back(options_buffer,
		       options.MakeId(options_buffer));

	return b(pool);
}

/*
 * libevent callback
 *
 */

void
FcgiConnection::OnSocketEvent(unsigned) noexcept
{
	char buffer;
	ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
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

Event::Duration
FcgiStock::GetChildClearInterval(void *info) const noexcept
{
	const auto &params = *(const FcgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(10)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

bool
FcgiStock::WantReturnStderr(void *info) const noexcept
{
	const auto &params = *(const FcgiChildParams *)info;
	/* we need the child process to return the stderr_fd to us if
	   the given path is "jailed" */
	return params.options.stderr_path != nullptr &&
		params.options.stderr_jailed;
}

bool
FcgiStock::WantStderrPond(void *info) const noexcept
{
	const auto &params = *(const FcgiChildParams *)info;
	return params.options.stderr_pond;
}

StringView
FcgiStock::GetChildTag(void *info) const noexcept
{
	const auto &params = *(const FcgiChildParams *)info;

	return params.options.tag;
}

void
FcgiStock::PrepareChild(void *info, UniqueSocketDescriptor &&fd,
			PreparedChildProcess &p)
{
	auto &params = *(FcgiChildParams *)info;
	const ChildOptions &options = params.options;

	p.SetStdin(std::move(fd));

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

/*
 * stock class
 *
 */

void
FcgiStock::Create(CreateStockItem c, StockRequest request,
		  [[maybe_unused]] CancellablePointer &cancel_ptr)
{
	[[maybe_unused]] auto &params = *(FcgiChildParams *)request.get();
	assert(params.executable_path != nullptr);

	auto *connection = new FcgiConnection(GetEventLoop(), c);

	const char *key = c.GetStockName();

	try {
		connection->child = child_stock.GetStockMap().GetNow(key,
								     std::move(request));
	} catch (...) {
		delete connection;
		std::throw_with_nested(FcgiClientError(StringFormat<256>("Failed to start FastCGI server '%s'",
									 key)));
	}

	try {
		connection->fd = child_stock_item_connect(*connection->child);
	} catch (...) {
		connection->kill = true;
		delete connection;
		std::throw_with_nested(FcgiClientError(StringFormat<256>("Failed to connect to FastCGI server '%s'",
									 key)));
	}

	connection->event.Open(connection->fd);

	connection->InvokeCreateSuccess();
}

bool
FcgiConnection::Borrow() noexcept
{
	/* check the connection status before using it, just in case the
	   FastCGI server has decided to close the connection before
	   fcgi_connection_event_callback() got invoked */
	char buffer;
	ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
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
		child->Put(kill);
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
		     *this,
		     4,
		     _log_socket, _log_options,
		     limit, max_idle) {}

void
FcgiStock::FadeTag(StringView tag) noexcept
{
	assert(tag != nullptr);

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

SocketDescriptor
fcgi_stock_get_log_socket(const FcgiStock &fs) noexcept
{
	return fs.GetLogSocket();
}

const ChildErrorLogOptions &
fcgi_stock_get_log_options(const FcgiStock &fs) noexcept
{
	return fs.GetLogOptions();
}

void
fcgi_stock_fade_all(FcgiStock &fs) noexcept
{
	fs.FadeAll();
}

void
fcgi_stock_fade_tag(FcgiStock &fs, StringView tag) noexcept
{
	fs.FadeTag(tag);
}

inline StockItem *
FcgiStock::Get(const ChildOptions &options,
	       const char *executable_path,
	       ConstBuffer<const char *> args)
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<FcgiChildParams>(*tpool, executable_path,
						       args, options);
	const char *key = r->GetStockKey(*tpool);
	return hstock.GetNow(key, std::move(r));
}

StockItem *
fcgi_stock_get(FcgiStock *fcgi_stock,
	       const ChildOptions &options,
	       const char *executable_path,
	       ConstBuffer<const char *> args)
{
	return fcgi_stock->Get(options, executable_path, args);
}

int
fcgi_stock_item_get_domain(gcc_unused const StockItem &item) noexcept
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
