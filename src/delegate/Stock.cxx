// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Stock.hxx"
#include "stock/MapStock.hxx"
#include "stock/Class.hxx"
#include "stock/Item.hxx"
#include "system/Error.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "event/SocketEvent.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ProcessHandle.hxx"
#include "AllocatorPtr.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/tpool.hxx"
#include "io/FdHolder.hxx"
#include "io/Logger.hxx"

#include <unistd.h>
#include <sys/socket.h>

struct DelegateArgs {
	const char *executable_path;

	const ChildOptions &options;

	DelegateArgs(const char *_executable_path,
		     const ChildOptions &_options) noexcept
		:executable_path(_executable_path), options(_options) {}

	const char *GetStockKey(AllocatorPtr alloc) const noexcept {
		const char *key = executable_path;

		char options_buffer[16384];
		char *options_end = options.MakeId(options_buffer);
		if (options_end > options_buffer)
			key = alloc.Concat(key, '|',
					   std::string_view{options_buffer, std::size_t(options_end - options_buffer)});

		return key;
	}
};

class DelegateProcess final : public StockItem {
	const LLogger logger;

	std::unique_ptr<ChildProcessHandle> handle;

	SocketEvent event;

public:
	explicit DelegateProcess(CreateStockItem c,
				 std::unique_ptr<ChildProcessHandle> &&_handle,
				 UniqueSocketDescriptor &&_fd) noexcept
		:StockItem(c),
		 logger(c.GetStockName()),
		 handle(std::move(_handle)),
		 event(c.stock.GetEventLoop(),
		       BIND_THIS_METHOD(SocketEventCallback), _fd.Release())
	{
	}

	~DelegateProcess() noexcept {
		event.Close();
	}

	SocketDescriptor GetSocket() const noexcept {
		return event.GetSocket();
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
	explicit DelegateStock(EventLoop &event_loop,
			       SpawnService &_spawn_service) noexcept
		:spawn_service(_spawn_service),
		 stock(event_loop, *this, 0, 16,
		       std::chrono::minutes(2)) {}

	StockMap &GetStock() noexcept {
		return stock;
	}

private:
	/* virtual methods from class StockClass */
	void Create(CreateStockItem c, StockRequest request,
		    StockGetHandler &handler,
		    CancellablePointer &cancel_ptr) override;
};

/*
 * libevent callback
 *
 */

inline void
DelegateProcess::SocketEventCallback(unsigned) noexcept
{
	std::byte buffer[1];
	ssize_t nbytes = GetSocket().Receive(buffer, MSG_DONTWAIT);
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
		      StockGetHandler &handler,
		      CancellablePointer &)
{
	auto &info = *(DelegateArgs *)request.get();

	FdHolder close_fds;
	PreparedChildProcess p;
	p.Append(info.executable_path);

	info.options.CopyTo(p, close_fds);

	auto [server_fd, client_fd] = CreateStreamSocketPair();

	p.stdin_fd = server_fd.ToFileDescriptor();

	auto handle = spawn_service.SpawnChildProcess(info.executable_path,
						      std::move(p));

	/* invoke the DelegateArgs destructor before invoking the
	   callback, because the latter may destroy the pool */
	request.reset();

	auto *process = new DelegateProcess(c, std::move(handle),
					    std::move(client_fd));
	process->InvokeCreateSuccess(handler);
}

/*
 * interface
 *
 */

StockMap *
delegate_stock_new(EventLoop &event_loop, SpawnService &spawn_service) noexcept
{
	auto *stock = new DelegateStock(event_loop, spawn_service);
	return &stock->GetStock();
}

void
delegate_stock_free(StockMap *_stock) noexcept
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
