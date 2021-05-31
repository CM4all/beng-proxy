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
#include "Launch.hxx"
#include "IdleConnection.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "pool/StringBuilder.hxx"
#include "io/Logger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringList.hxx"

#include <string>

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <stdlib.h>

struct WasChildParams {
	const char *executable_path;

	ConstBuffer<const char *> args;

	const ChildOptions &options;

	WasChildParams(const char *_executable_path,
		       ConstBuffer<const char *> _args,
		       const ChildOptions &_options) noexcept
		:executable_path(_executable_path), args(_args),
		 options(_options) {}

	const char *GetStockKey(struct pool &pool) const noexcept;
};

class WasChild final : public StockItem, ExitListener, WasIdleConnectionHandler {
	const LLogger logger;

	SpawnService &spawn_service;

	const std::string tag;

	ChildErrorLog log;

	int pid = -1;

	WasIdleConnection connection;

public:
	explicit WasChild(CreateStockItem c, SpawnService &_spawn_service,
			  std::string_view _tag) noexcept
		:StockItem(c), logger(GetStockName()), spawn_service(_spawn_service),
		 tag(_tag),
		 connection(c.stock.GetEventLoop(), *this)
	{
	}

	~WasChild() noexcept override;

	auto &GetEventLoop() const noexcept {
		return connection.GetEventLoop();
	}

	bool IsTag(StringView other_tag) const noexcept {
		return StringListContains(tag, '\0', other_tag);
	}

	/**
	 * Throws on error.
	 */
	void Launch(const WasChildParams &params, SocketDescriptor log_socket,
		    const ChildErrorLogOptions &log_options) {
		auto process =
			was_launch(spawn_service,
				   GetStockName(),
				   params.executable_path,
				   params.args,
				   params.options,
				   log.EnableClient(GetEventLoop(),
						    log_socket, log_options,
						    params.options.stderr_pond),
				   this);

		pid = process.pid;

		WasSocket &socket = process;
		connection.Open(std::move(socket));
	}

	void SetSite(const char *_site) noexcept {
		log.SetSite(_site);
	}

	void SetUri(const char *_uri) noexcept {
		log.SetUri(_uri);
	}

	const WasSocket &GetSocket() const noexcept {
		return connection.GetSocket();
	}

	void Stop(uint64_t _received) noexcept {
		assert(!is_idle);

		connection.Stop(_received);
	}

public:
	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		return connection.Borrow();
	}

	bool Release() noexcept override {
		connection.Release();
		unclean = connection.IsStopping();
		return true;
	}

private:
	/* virtual methods from class ExitListener */
	void OnChildProcessExit(gcc_unused int status) noexcept override {
		pid = -1;
	}

	/* virtual methods from class WasIdleConnectionHandler */
	void OnWasIdleConnectionClean() noexcept override {
		ClearUncleanFlag();
	}

	void OnWasIdleConnectionError(std::exception_ptr e) noexcept override {
		logger(2, e);
		InvokeIdleDisconnect();
	}
};

const char *
WasChildParams::GetStockKey(struct pool &pool) const noexcept
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
 * stock class
 *
 */

void
WasStock::FadeTag(StringView tag) noexcept
{
	stock.FadeIf([tag](const StockItem &item){
		const auto &child = (const WasChild &)item;
		return child.IsTag(tag);
	});
}

void
WasStock::Create(CreateStockItem c, StockRequest _request,
		 gcc_unused CancellablePointer &cancel_ptr)
{
	auto &params = *(WasChildParams *)_request.get();

	assert(params.executable_path != nullptr);

	auto *child = new WasChild(c, spawn_service, params.options.tag);

	try {
		child->Launch(params, log_socket, log_options);
	} catch (...) {
		delete child;
		throw;
	}

	/* invoke the WasChildParams destructor before invoking the
	   callback, because the latter may destroy the pool */
	_request.reset();

	child->InvokeCreateSuccess();
}

WasChild::~WasChild() noexcept
{
	if (pid >= 0)
		spawn_service.KillChildProcess(pid);
}

/*
 * interface
 *
 */

void
WasStock::Get(struct pool &pool,
	      const ChildOptions &options,
	      const char *executable_path,
	      ConstBuffer<const char *> args,
	      StockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<WasChildParams>(pool, executable_path,
						      args, options);
	const char *key = r->GetStockKey(*tpool);

	stock.Get(key, std::move(r), handler, cancel_ptr);
}

void
was_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &child = (WasChild &)item;
	child.SetSite(site);
}

void
was_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &child = (WasChild &)item;
	child.SetUri(uri);
}

const WasSocket &
was_stock_item_get(const StockItem &item) noexcept
{
	auto *child = (const WasChild *)&item;

	return child->GetSocket();
}

void
was_stock_item_stop(StockItem &item, uint64_t received) noexcept
{
	auto &child = (WasChild &)item;
	child.Stop(received);
}
