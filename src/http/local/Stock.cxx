/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "Address.hxx"
#include "stock/Stock.hxx"
#include "stock/MultiStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "pool/tpool.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "spawn/ListenChildStock.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/Exception.hxx"
#include "util/StringList.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

class LhttpStock final : MultiStockClass, ListenChildStockClass {
	ChildStock child_stock;
	MultiStock mchild_stock;

public:
	LhttpStock(unsigned limit, unsigned max_idle,
		   EventLoop &event_loop, SpawnService &spawn_service,
		   SocketDescriptor log_socket,
		   const ChildErrorLogOptions &log_options) noexcept;

	void DiscardSome() noexcept {
		/* first close idle connections, hopefully turning
		   child processes idle */
		mchild_stock.DiscardUnused();

		/* kill the oldest child process */
		child_stock.DiscardOldestIdle();
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
	bool WantStderrPond(void *info) const noexcept override;
	std::string_view GetChildTag(void *info) const noexcept override;
	void PrepareChild(void *info, PreparedChildProcess &p) override;

	/* virtual methods from class ChildStockMapClass */
	std::size_t GetChildLimit(const void *request,
				  std::size_t _limit) const noexcept override;
	Event::Duration GetChildClearInterval(const void *info) const noexcept override;

	/* virtual methods from class ListenChildStockClass */
	int GetChildSocketType(void *info) const noexcept override;
	unsigned GetChildBacklog(void *info) const noexcept override;
	void PrepareListenChild(void *info, UniqueSocketDescriptor fd,
				PreparedChildProcess &p) override;
};

class LhttpConnection final
	: LoggerDomainFactory, public StockItem
{
	LazyDomainLogger logger;

	ListenChildStockItem &child;

	LeasePtr lease_ref;

	SocketEvent event;

public:
	explicit LhttpConnection(CreateStockItem c,
				 ListenChildStockItem &_child)
		:StockItem(c),
		 logger(*this),
		 child(_child),
		 event(c.stock.GetEventLoop(),
		       BIND_THIS_METHOD(EventCallback),
		       _child.Connect().Release()) {}

	~LhttpConnection() noexcept override;

	SocketDescriptor GetSocket() const noexcept {
		assert(event.IsDefined());
		return event.GetSocket();
	}

	void AbandonSocket() noexcept {
		assert(event.IsDefined());
		assert(event.GetScheduledFlags() == 0);

		event.Abandon();
	}

	gcc_pure
	std::string_view GetTag() const noexcept {
		return child.GetTag();
	}

	void SetSite(const char *site) noexcept {
		child.SetSite(site);
	}

	void SetUri(const char *uri) noexcept {
		child.SetUri(uri);
	}

private:
	void Read() noexcept;
	void EventCallback(unsigned events) noexcept;

	/* virtual methods from LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override {
		return GetStockName();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		if (event.GetReadyFlags() != 0) [[unlikely]] {
			/* this connection was probably closed, but
			   our SocketEvent callback hasn't been
			   invoked yet; refuse to use this item; the
			   caller will destroy the connection */
			Read();
			return false;
		}

		event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		return true;
	}
};

static const char *
lhttp_stock_key(struct pool *pool, const LhttpAddress *address) noexcept
{
	return address->GetServerId(AllocatorPtr(*pool));
}

inline void
LhttpConnection::Read() noexcept
{
	char buffer;
	ssize_t nbytes = GetSocket().Read(&buffer, sizeof(buffer));
	if (nbytes < 0)
		logger(2, "error on idle LHTTP connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle LHTTP connection");
}

/*
 * libevent callback
 *
 */

inline void
LhttpConnection::EventCallback(unsigned) noexcept
{
	Read();
	InvokeIdleDisconnect();
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

bool
LhttpStock::WantStderrPond(void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;
	return address.options.stderr_pond;
}

int
LhttpStock::GetChildSocketType(void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	int type = SOCK_STREAM;
	if (!address.blocking)
		type |= SOCK_NONBLOCK;

	return type;
}

unsigned
LhttpStock::GetChildBacklog(void *info) const noexcept
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
LhttpStock::GetChildTag(void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	return address.options.tag;
}

void
LhttpStock::PrepareChild(void *info, PreparedChildProcess &p)
{
	const auto &address = *(const LhttpAddress *)info;

	address.CopyTo(p);
}

void
LhttpStock::PrepareListenChild(void *, UniqueSocketDescriptor fd,
			       PreparedChildProcess &p)
{
	p.SetStdin(std::move(fd));
}

/*
 * stock class
 *
 */

StockItem *
LhttpStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &child = (ListenChildStockItem &)shared_item;

	return new LhttpConnection(c, child);
}

LhttpConnection::~LhttpConnection() noexcept
{
	event.Close();
}


/*
 * interface
 *
 */

inline
LhttpStock::LhttpStock(unsigned limit, unsigned max_idle,
		       EventLoop &event_loop, SpawnService &spawn_service,
		       SocketDescriptor log_socket,
		       const ChildErrorLogOptions &log_options) noexcept
	:child_stock(spawn_service, *this,
		     log_socket, log_options),
	 mchild_stock(event_loop, child_stock,
		      limit, max_idle, *this)
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
		SocketDescriptor log_socket,
		const ChildErrorLogOptions &log_options) noexcept
{
	return new LhttpStock(limit, max_idle, event_loop, spawn_service,
			      log_socket, log_options);
}

void
lhttp_stock_free(LhttpStock *ls) noexcept
{
	delete ls;
}

void
lhttp_stock_discard_some(LhttpStock &ls) noexcept
{
	ls.DiscardSome();
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

SocketDescriptor
lhttp_stock_item_get_socket(const StockItem &item) noexcept
{
	const auto *connection = (const LhttpConnection *)&item;

	return connection->GetSocket();
}

void
lhttp_stock_item_abandon_socket(StockItem &item) noexcept
{
	auto &connection = static_cast<LhttpConnection &>(item);

	connection.AbandonSocket();
}

FdType
lhttp_stock_item_get_type(gcc_unused const StockItem &item) noexcept
{
	return FdType::FD_SOCKET;
}

void
lhttp_stock_item_set_site(StockItem &item, const char *site) noexcept
{
	auto &connection = (LhttpConnection &)item;
	connection.SetSite(site);
}

void
lhttp_stock_item_set_uri(StockItem &item, const char *uri) noexcept
{
	auto &connection = (LhttpConnection &)item;
	connection.SetUri(uri);
}
