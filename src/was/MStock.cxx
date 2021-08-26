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
#include "IdleConnection.hxx"
#include "was/async/Socket.hxx"
#include "was/async/MultiClient.hxx"
#include "cgi/Address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Item.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ChildStockItem.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringList.hxx"

#include <cassert>

class MultiWasChild final : public ChildStockItem {
	SocketEvent event;

public:
	MultiWasChild(CreateStockItem c,
		      ChildStock &_child_stock,
		      std::string_view _tag) noexcept
		:ChildStockItem(c, _child_stock, _tag),
		 event(c.stock.GetEventLoop(), BIND_THIS_METHOD(OnSocketReady))
	{}

	~MultiWasChild() noexcept {
		event.Close();
	}

	WasSocket Connect();

protected:
	/* virtual methods from class ChildStockItem */
	void Prepare(ChildStockClass &cls, void *info,
		     PreparedChildProcess &p) override;

private:
	void OnSocketReady(unsigned events) noexcept;

	void Connect(WasSocket &&socket);
};

void
MultiWasChild::Prepare(ChildStockClass &cls, void *info,
		       PreparedChildProcess &p)
{
	assert(!event.IsDefined());

	ChildStockItem::Prepare(cls, info, p);

	UniqueSocketDescriptor for_child, for_parent;
	if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						      for_child, for_parent))
		throw MakeErrno("socketpair() failed");

	p.SetStdin(std::move(for_child));

	event.Open(for_parent.Release());
	event.ScheduleImplicit();
}

void
MultiWasChild::OnSocketReady(unsigned events) noexcept
{
	(void)events; // TODO
	event.Close();
	Disconnected();
}

inline void
MultiWasChild::Connect(WasSocket &&socket)
{
	Was::SendMultiNew(event.GetSocket(), std::move(socket));
}

WasSocket
MultiWasChild::Connect()
{
	auto [result, for_child] = WasSocket::CreatePair();
	Connect(std::move(for_child));
	return std::move(result);
}

class MultiWasConnection final : LoggerDomainFactory, StockItem,
				 WasIdleConnectionHandler {
	LazyDomainLogger logger;

	MultiWasChild *child = nullptr;

	LeasePtr lease_ref;

	WasIdleConnection connection;

public:
	explicit MultiWasConnection(CreateStockItem c) noexcept
		:StockItem(c),
		 logger(*this),
		 connection(c.stock.GetEventLoop(), *this) {}

	~MultiWasConnection() noexcept override;

	void Connect(MultiStock &child_stock,
		     const char *key, StockRequest &&request,
		     unsigned concurrency);

	const auto &GetSocket() const noexcept {
		return connection.GetSocket();
	}

	void Stop(uint64_t _received) noexcept {
		assert(!is_idle);

		connection.Stop(_received);
	}

	[[gnu::pure]]
	StringView GetTag() const noexcept {
		assert(child != nullptr);

		return child->GetTag();
	}

	void SetSite(const char *site) noexcept {
		assert(child != nullptr);

		child->SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		assert(child != nullptr);

		child->SetUri(uri);
	}

private:
	void EventCallback(unsigned events) noexcept;

	/* virtual methods from LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override {
		return GetStockName();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		return connection.Borrow();
	}

	bool Release() noexcept override {
		connection.Release();
		unclean = connection.IsStopping();
		return true;
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

inline void
MultiWasConnection::Connect(MultiStock &child_stock,
			    const char *key, StockRequest &&request,
			    unsigned concurrency)
{
	try {
		child = (MultiWasChild *)
			child_stock.GetNow(key, std::move(request), concurrency,
					   lease_ref);
	} catch (...) {
		delete this;
		std::throw_with_nested(FormatRuntimeError("Failed to launch MultiWAS server '%s'",
							  key));
	}

	try {
		connection.Open(child->Connect());
	} catch (...) {
		delete this;
		std::throw_with_nested(FormatRuntimeError("Failed to connect to LHTTP server '%s'",
							  key));
	}

	InvokeCreateSuccess();
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
	 hstock(event_loop, *this, limit, max_idle,
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
		      CancellablePointer &)
{
	const auto &params = *(const CgiChildParams *)request.get();

	auto *connection = new MultiWasConnection(c);
	connection->Connect(mchild_stock,
			    c.GetStockName(), std::move(request),
			    params.concurrency);
}

MultiWasConnection::~MultiWasConnection() noexcept
{
	if (child != nullptr)
		lease_ref.Release(true);
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

const WasSocket &
mwas_stock_item_get_socket(const StockItem &item) noexcept
{
	const auto &connection = (const MultiWasConnection &)item;
	return connection.GetSocket();
}

void
mwas_stock_item_stop(StockItem &item, uint64_t received) noexcept
{
	auto &connection = (MultiWasConnection &)item;
	connection.Stop(received);
}

void
mwas_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &connection = (MultiWasConnection &)item;
	connection.SetSite(site);
}

void
mwas_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &connection = (MultiWasConnection &)item;
	connection.SetUri(uri);
}
