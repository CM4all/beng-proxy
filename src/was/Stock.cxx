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
#include "SConnection.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringList.hxx"

#include <cassert>
#include <string>

class WasChild final : public WasStockConnection, ExitListener {
	SpawnService &spawn_service;

	const std::string tag;

	ChildErrorLog log;

	int pid = -1;

public:
	explicit WasChild(CreateStockItem c, SpawnService &_spawn_service,
			  std::string_view _tag) noexcept
		:WasStockConnection(c), spawn_service(_spawn_service),
		 tag(_tag)
	{
	}

	~WasChild() noexcept override;

	bool IsTag(StringView other_tag) const noexcept {
		return StringListContains(tag, '\0', other_tag);
	}

	/**
	 * Throws on error.
	 */
	void Launch(const CgiChildParams &params, SocketDescriptor log_socket,
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
		Open(std::move(socket));
	}

	void SetSite(const char *_site) noexcept override {
		log.SetSite(_site);
	}

	void SetUri(const char *_uri) noexcept override {
		log.SetUri(_uri);
	}

private:
	/* virtual methods from class ExitListener */
	void OnChildProcessExit(gcc_unused int status) noexcept override {
		pid = -1;
	}
};

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
	auto &params = *(CgiChildParams *)_request.get();

	assert(params.executable_path != nullptr);

	auto *child = new WasChild(c, spawn_service, params.options.tag);

	try {
		child->Launch(params, log_socket, log_options);
	} catch (...) {
		delete child;
		throw;
	}

	/* invoke the CgiChildParams destructor before invoking the
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

	auto r = NewDisposablePointer<CgiChildParams>(pool, executable_path,
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
