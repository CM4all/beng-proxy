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

#include "MStock.hxx"
#include "SConnection.hxx"
#include "was/async/Socket.hxx"
#include "was/async/MultiClient.hxx"
#include "cgi/Address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ChildStockItem.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringList.hxx"

#include <cassert>
#include <optional>

class MultiWasChild final : public ChildStockItem, Was::MultiClientHandler {
	EventLoop &event_loop;

	std::optional<Was::MultiClient> client;

public:
	MultiWasChild(CreateStockItem c,
		      ChildStock &_child_stock,
		      std::string_view _tag) noexcept
		:ChildStockItem(c, _child_stock, _tag),
		 event_loop(c.stock.GetEventLoop())
	{}

	WasSocket Connect() {
		return client->Connect();
	}

protected:
	/* virtual methods from class ChildStockItem */
	void Prepare(ChildStockClass &cls, void *info,
		     PreparedChildProcess &p) override;

private:
	/* virtual methods from class Was::MultiClientHandler */
	void OnMultiClientDisconnect() noexcept override {
		client.reset();
		Disconnected();
	}

	void OnMultiClientError(std::exception_ptr error) noexcept override {
		(void)error; // TODO log error?
		client.reset();
		Disconnected();
	}
};

void
MultiWasChild::Prepare(ChildStockClass &cls, void *info,
		       PreparedChildProcess &p)
{
	assert(!client);

	ChildStockItem::Prepare(cls, info, p);

	UniqueSocketDescriptor for_child, for_parent;
	if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						      for_child, for_parent))
		throw MakeErrno("socketpair() failed");

	p.SetStdin(std::move(for_child));

	Was::MultiClientHandler &client_handler = *this;
	client.emplace(event_loop, std::move(for_parent), client_handler);
}

class MultiWasConnection final
	: public WasStockConnection, Cancellable, StockGetHandler
{
	MultiWasChild *child = nullptr;

	CancellablePointer get_cancel_ptr;

	LeasePtr lease_ref;

public:
	using WasStockConnection::WasStockConnection;

	~MultiWasConnection() noexcept override;

	void Connect(MultiStock &child_stock,
		     StockRequest &&request,
		     unsigned concurrency,
		     CancellablePointer &cancel_ptr) noexcept;

	[[gnu::pure]]
	StringView GetTag() const noexcept {
		assert(child != nullptr);

		return child->GetTag();
	}

	void SetSite(const char *site) noexcept override {
		assert(child != nullptr);

		child->SetSite(site);
	}

	void SetUri(const char *uri) noexcept override {
		assert(child != nullptr);

		child->SetUri(uri);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		InvokeCreateAborted();
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr error) noexcept override;
};

inline void
MultiWasConnection::Connect(MultiStock &child_stock,
			    StockRequest &&request,
			    unsigned concurrency,
			    CancellablePointer &caller_cancel_ptr) noexcept
{
	caller_cancel_ptr = *this;

	child_stock.Get(GetStockName(), std::move(request), concurrency,
			lease_ref, *this, get_cancel_ptr);
}

void
MultiWasConnection::OnStockItemReady(StockItem &item) noexcept
{
	get_cancel_ptr = nullptr;

	child = (MultiWasChild *)&item;

	try {
		Open(child->Connect());
	} catch (...) {
		InvokeCreateError(NestException(std::current_exception(),
						FormatRuntimeError("Failed to connect to MultiWAS server '%s'",
								   GetStockName())));
		return;
	}

	InvokeCreateSuccess();
}

void
MultiWasConnection::OnStockItemError(std::exception_ptr error) noexcept
{
	get_cancel_ptr = nullptr;

	InvokeCreateError(NestException(std::move(error),
					FormatRuntimeError("Failed to launch MultiWAS server %s",
							   GetStockName())));
}

MultiWasStock::MultiWasStock(unsigned limit, unsigned max_idle,
			     EventLoop &event_loop, SpawnService &spawn_service,
			     SocketDescriptor log_socket,
			     const ChildErrorLogOptions &log_options) noexcept
	:child_stock(event_loop, spawn_service,
		     *this,
		     log_socket, log_options,
		     limit, max_idle),
	 mchild_stock(child_stock.GetStockMap()),
	 hstock(event_loop, *this, 0, max_idle,
		std::chrono::minutes{2}) {}

Event::Duration
MultiWasStock::GetChildClearInterval(void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(15)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

bool
MultiWasStock::WantStderrPond(void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;
	return params.options.stderr_pond;
}

StringView
MultiWasStock::GetChildTag(void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.tag;
}

std::unique_ptr<ChildStockItem>
MultiWasStock::CreateChild(CreateStockItem c,
			   void *info,
			   ChildStock &_child_stock)
{
	return std::make_unique<MultiWasChild>(c, _child_stock,
					       GetChildTag(info));
}

void
MultiWasStock::PrepareChild(void *info, PreparedChildProcess &p)
{
	const auto &params = *(const CgiChildParams *)info;

	p.Append(params.executable_path);
	for (auto i : params.args)
		p.Append(i);

	params.options.CopyTo(p);
}

void
MultiWasStock::Create(CreateStockItem c, StockRequest request,
		      CancellablePointer &cancel_ptr)
{
	const auto &params = *(const CgiChildParams *)request.get();

	auto *connection = new MultiWasConnection(c);
	connection->Connect(mchild_stock, std::move(request),
			    params.concurrency,
			    cancel_ptr);
}

MultiWasConnection::~MultiWasConnection() noexcept
{
	if (child != nullptr)
		lease_ref.Release(true);
	else if (get_cancel_ptr)
		get_cancel_ptr.CancelAndClear();
}

void
MultiWasStock::FadeTag(StringView tag) noexcept
{
	assert(tag != nullptr);

	hstock.FadeIf([tag](const StockItem &item){
		const auto &connection = (const MultiWasConnection &)item;
		return StringListContains(connection.GetTag(), '\0', tag);
	});

	mchild_stock.FadeIf([tag](const StockItem &item){
		const auto &child = (const MultiWasChild &)item;
		return child.IsTag(tag);
	});

	child_stock.FadeTag(tag);
}

void
MultiWasStock::Get(AllocatorPtr alloc,
		   const ChildOptions &options,
		   const char *executable_path,
		   ConstBuffer<const char *> args,
		   unsigned concurrency,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	const TempPoolLease tpool;

	auto r = NewDisposablePointer<CgiChildParams>(alloc, executable_path,
						      args, options,
						      concurrency);
	const char *key = r->GetStockKey(*tpool);

	GetConnectionStock().Get(key, std::move(r), handler, cancel_ptr);
}
