/*
 * Copyright 2007-2020 CM4all GmbH
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
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "io/Logger.hxx"

#include <assert.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>

struct DelegateArgs {
	const char *executable_path;

	const ChildOptions &options;

	DelegateArgs(const char *_executable_path,
		     const ChildOptions &_options)
		:executable_path(_executable_path), options(_options) {}

	const char *GetStockKey(AllocatorPtr alloc) const {
		const char *key = executable_path;

		char options_buffer[16384];
		char *options_end = options.MakeId(options_buffer);
		if (options_end > options_buffer)
			key = alloc.Concat(key, '|',
					   StringView{options_buffer, options_end});

		return key;
	}
};

class DelegateProcess final : public StockItem {
	const LLogger logger;

	UniqueSocketDescriptor fd;

	SocketEvent event;

public:
	explicit DelegateProcess(CreateStockItem c,
				 UniqueSocketDescriptor &&_fd) noexcept
		:StockItem(c),
		 logger(c.GetStockName()),
		 fd(std::move(_fd)),
		 event(c.stock.GetEventLoop(),
		       BIND_THIS_METHOD(SocketEventCallback), fd)
	{
	}

	SocketDescriptor GetSocket() const noexcept {
		return fd;
	}

	/* virtual methods from class StockItem */
	bool Borrow() noexcept override {
		event.Cancel();
		return true;
	}

	bool Release() noexcept override {
		event.ScheduleRead();
		return true;
	}

private:
	void SocketEventCallback(unsigned events) noexcept;
};

class DelegateStock final : StockClass {
	SpawnService &spawn_service;
	StockMap stock;

public:
	explicit DelegateStock(EventLoop &event_loop, SpawnService &_spawn_service)
		:spawn_service(_spawn_service),
		 stock(event_loop, *this, 0, 16) {}

	StockMap &GetStock() {
		return stock;
	}

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    CancellablePointer &cancel_ptr) override;
};

/*
 * libevent callback
 *
 */

inline void
DelegateProcess::SocketEventCallback(unsigned) noexcept
{
	char buffer;
	ssize_t nbytes = recv(fd.Get(), &buffer, sizeof(buffer), MSG_DONTWAIT);
	if (nbytes < 0)
		logger(2, "error on idle delegate process: ", strerror(errno));
	else if (nbytes > 0)
		logger(2, "unexpected data from idle delegate process");

	InvokeIdleDisconnect();
}

/*
 * stock class
 *
 */

void
DelegateStock::Create(CreateStockItem c,
		      StockRequest request,
		      gcc_unused CancellablePointer &cancel_ptr)
{
	auto &info = *(DelegateArgs *)request.get();

	PreparedChildProcess p;
	p.Append(info.executable_path);

	info.options.CopyTo(p, true, nullptr);

	UniqueSocketDescriptor server_fd, client_fd;
	if (!UniqueSocketDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
						      server_fd, client_fd))
		throw MakeErrno("socketpair() failed");

	p.SetStdin(std::move(server_fd));

	spawn_service.SpawnChildProcess(info.executable_path,
					std::move(p), nullptr);

	auto *process = new DelegateProcess(c, std::move(client_fd));
	process->InvokeCreateSuccess();
}

/*
 * interface
 *
 */

StockMap *
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service)
{
	auto *stock = new DelegateStock(event_loop, spawn_service);
	return &stock->GetStock();
}

void
delegate_stock_free(StockMap *_stock)
{
	auto *stock = (DelegateStock *)&_stock->GetClass();
	delete stock;
}

StockItem *
delegate_stock_get(StockMap *delegate_stock,
		   const char *helper,
		   const ChildOptions &options)
{
	const TempPoolLease tpool;
	const AllocatorPtr alloc(tpool);
	auto r = NewDisposablePointer<DelegateArgs>(alloc, helper, options);
	const char *key = r->GetStockKey(alloc);
	return delegate_stock->GetNow(key, std::move(r));
}

SocketDescriptor
delegate_stock_item_get(StockItem &item) noexcept
{
	auto *process = (DelegateProcess *)&item;

	return process->GetSocket();
}
