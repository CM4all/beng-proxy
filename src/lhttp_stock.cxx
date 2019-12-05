/*
 * Copyright 2007-2017 Content Management AG
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

#include "lhttp_stock.hxx"
#include "lhttp_address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/MultiStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "pool/tpool.hxx"
#include "AllocatorPtr.hxx"
#include "lease.hxx"
#include "child_stock.hxx"
#include "spawn/JailParams.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/Logger.hxx"
#include "util/RuntimeError.hxx"
#include "util/Exception.hxx"

#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

class LhttpStock final : StockClass, ChildStockClass {
	ChildStock child_stock;
	MultiStock mchild_stock;
	StockMap hstock;

public:
	LhttpStock(unsigned limit, unsigned max_idle,
		   EventLoop &event_loop, SpawnService &spawn_service,
		   SocketDescriptor log_socket,
		   const ChildErrorLogOptions &log_options) noexcept;

	void FadeAll() noexcept {
		hstock.FadeAll();
		child_stock.GetStockMap().FadeAll();
		mchild_stock.FadeAll();
	}

	void FadeTag(const char *tag) noexcept;

	StockMap &GetConnectionStock() noexcept {
		return hstock;
	}

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;

	/* virtual methods from class ChildStockClass */
	int GetChildSocketType(void *info) const noexcept override;
	const char *GetChildTag(void *info) const noexcept override;
	void PrepareChild(void *info, UniqueSocketDescriptor &&fd,
			  PreparedChildProcess &p) override;
};

class LhttpConnection final : LoggerDomainFactory, StockItem {
	LazyDomainLogger logger;

	StockItem *child = nullptr;

	LeasePtr lease_ref;

	UniqueSocketDescriptor fd;
	SocketEvent event;
	TimerEvent idle_timeout_event;

public:
	explicit LhttpConnection(CreateStockItem c) noexcept
		:StockItem(c),
		 logger(*this),
		 event(c.stock.GetEventLoop(), BIND_THIS_METHOD(EventCallback)),
		 idle_timeout_event(c.stock.GetEventLoop(),
				    BIND_THIS_METHOD(OnIdleTimeout)) {}

	~LhttpConnection() noexcept override;

	void Connect(MultiStock &child_stock,
		     const char *key, StockRequest &&request,
		     unsigned concurrency);

	SocketDescriptor GetSocket() const noexcept {
		assert(fd.IsDefined());
		return fd;
	}

	gcc_pure
	const char *GetTag() const noexcept {
		assert(child != nullptr);

		return child_stock_item_get_tag(*child);
	}

	void SetSite(const char *site) noexcept {
		child_stock_item_set_site(*child, site);
	}

	void SetUri(const char *uri) noexcept {
		child_stock_item_set_uri(*child, uri);
	}

private:
	void EventCallback(unsigned events) noexcept;
	void OnIdleTimeout() noexcept;

	/* virtual methods from LoggerDomainFactory */
	std::string MakeLoggerDomain() const noexcept override {
		return GetStockName();
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		event.Cancel();
		idle_timeout_event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		idle_timeout_event.Schedule(std::chrono::minutes(5));
		return true;
	}
};

inline void
LhttpConnection::Connect(MultiStock &child_stock,
			 const char *key, StockRequest &&request,
			 unsigned concurrency)
{
	try {
		child = child_stock.GetNow(key, std::move(request), concurrency,
					   lease_ref);
	} catch (...) {
		delete this;
		std::throw_with_nested(FormatRuntimeError("Failed to launch LHTTP server '%s'",
							  key));
	}

	try {
		fd = child_stock_item_connect(*child);
	} catch (...) {
		delete this;
		std::throw_with_nested(FormatRuntimeError("Failed to connect to LHTTP server '%s'",
							  key));
	}

	event.Open(fd);
	InvokeCreateSuccess();
}

static const char *
lhttp_stock_key(struct pool *pool, const LhttpAddress *address) noexcept
{
	return address->GetServerId(AllocatorPtr(*pool));
}

/*
 * libevent callback
 *
 */

inline void
LhttpConnection::EventCallback(unsigned) noexcept
{
	char buffer;
	ssize_t nbytes = fd.Read(&buffer, sizeof(buffer));
	if (nbytes < 0)
		logger(2, "error on idle LHTTP connection: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle LHTTP connection");

	InvokeIdleDisconnect();
}

inline void
LhttpConnection::OnIdleTimeout() noexcept
{
	InvokeIdleDisconnect();
}

/*
 * child_stock class
 *
 */

int
LhttpStock::GetChildSocketType(void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	int type = SOCK_STREAM;
	if (!address.blocking)
		type |= SOCK_NONBLOCK;

	return type;
}

const char *
LhttpStock::GetChildTag(void *info) const noexcept
{
	const auto &address = *(const LhttpAddress *)info;

	return address.options.tag;
}

void
LhttpStock::PrepareChild(void *info, UniqueSocketDescriptor &&fd,
			 PreparedChildProcess &p)
{
	const auto &address = *(const LhttpAddress *)info;

	p.SetStdin(std::move(fd));
	address.CopyTo(p);
}

/*
 * stock class
 *
 */

void
LhttpStock::Create(CreateStockItem c, StockRequest request,
		   gcc_unused CancellablePointer &cancel_ptr)
{
	const auto *address = (const LhttpAddress *)request.get();

	assert(address != nullptr);
	assert(address->path != nullptr);

	auto *connection = new LhttpConnection(c);

	connection->Connect(mchild_stock,
			    c.GetStockName(), std::move(request),
			    address->concurrency);
}

LhttpConnection::~LhttpConnection() noexcept
{
	if (fd.IsDefined()) {
		event.Cancel();
		fd.Close();
	}

	if (child != nullptr)
		lease_ref.Release(true);
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
	:child_stock(event_loop, spawn_service,
		     *this,
		     64,
		     log_socket, log_options,
		     limit, max_idle),
	 mchild_stock(child_stock.GetStockMap()),
	 hstock(event_loop, *this, limit, max_idle) {}

void
LhttpStock::FadeTag(const char *tag) noexcept
{
	assert(tag != nullptr);

	hstock.FadeIf([tag](const StockItem &item){
		const auto &connection = (const LhttpConnection &)item;
		const char *tag2 = connection.GetTag();
		return tag2 != nullptr && strcmp(tag, tag2) == 0;
	});

	mchild_stock.FadeIf([tag](const StockItem &item){
		const char *tag2 = child_stock_item_get_tag(item);
		return tag2 != nullptr && strcmp(tag, tag2) == 0;
	});

	child_stock.FadeTag(tag);
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
lhttp_stock_fade_all(LhttpStock &ls) noexcept
{
	ls.FadeAll();
}

void
lhttp_stock_fade_tag(LhttpStock &ls, const char *tag) noexcept
{
	ls.FadeTag(tag);
}

StockItem *
lhttp_stock_get(LhttpStock *lhttp_stock,
		const LhttpAddress *address)
{
	const auto *const jail = address->options.jail;
	if (jail != nullptr)
		jail->Check();

	const TempPoolLease tpool;
	return lhttp_stock->GetConnectionStock().GetNow(lhttp_stock_key(tpool, address),
							ToNopPointer(const_cast<LhttpAddress *>(address)));
}

SocketDescriptor
lhttp_stock_item_get_socket(const StockItem &item) noexcept
{
	const auto *connection = (const LhttpConnection *)&item;

	return connection->GetSocket();
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
