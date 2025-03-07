// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "MStock.hxx"
#include "SConnection.hxx"
#include "was/async/Socket.hxx"
#include "was/async/MultiClient.hxx"
#include "cgi/Address.hxx"
#include "stock/Stock.hxx"
#include "stock/MapStock.hxx"
#include "pool/DisposablePointer.hxx"
#include "pool/WithPoolDisposablePointer.hxx"
#include "AllocatorPtr.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ChildStockItem.hxx"
#include "spawn/Prepared.hxx"
#include "event/SocketEvent.hxx"
#include "net/SocketPair.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "system/Error.hxx"
#include "io/FdHolder.hxx"
#include "util/Exception.hxx"
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
	void Prepare(ChildStockClass &cls, const void *info,
		     PreparedChildProcess &p, FdHolder &close_fds) override;

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
MultiWasChild::Prepare(ChildStockClass &cls, const void *info,
		       PreparedChildProcess &p, FdHolder &close_fds)
{
	assert(!client);

	ChildStockItem::Prepare(cls, info, p, close_fds);

	auto [for_child, for_parent] = CreateSocketPair(SOCK_SEQPACKET);

	p.stdin_fd = close_fds.Insert(std::move(for_child).MoveToFileDescriptor());

	Was::MultiClientHandler &client_handler = *this;
	client.emplace(event_loop, std::move(for_parent), client_handler);
}

class MultiWasConnection final
	: public WasStockConnection
{
	MultiWasChild &child;

public:
	MultiWasConnection(CreateStockItem c, MultiWasChild &_child)
		:WasStockConnection(c, _child.Connect()),
		 child(_child) {}

	[[gnu::pure]]
	std::string_view GetTag() const noexcept {
		return child.GetTag();
	}

	void SetSite(const char *site) noexcept override {
		child.SetSite(site);
	}

	void SetUri(const char *uri) noexcept override {
		child.SetUri(uri);
	}
};

MultiWasStock::MultiWasStock(unsigned limit, [[maybe_unused]] unsigned max_idle,
			     EventLoop &event_loop, SpawnService &spawn_service,
#ifdef HAVE_LIBSYSTEMD
			     CgroupMultiWatch *_cgroup_multi_watch,
#endif
			     Net::Log::Sink *log_sink,
			     const ChildErrorLogOptions &log_options) noexcept
	:pool(pool_new_dummy(nullptr, "MultiWasStock")),
	 child_stock(spawn_service,
#ifdef HAVE_LIBSYSTEMD
		     _cgroup_multi_watch,
#endif
		     nullptr, // TODO do we need ListenStreamSpawnStock here?
		     *this,
		     log_sink, log_options),
	 mchild_stock(event_loop, child_stock,
		      limit,
		      // TODO max_idle,
		      *this) {}

std::size_t
MultiWasStock::GetLimit(const void *request,
			std::size_t _limit) const noexcept
{
	const auto &params = *(const CgiChildParams *)request;

	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

Event::Duration
MultiWasStock::GetClearInterval(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.ns.mount.pivot_root == nullptr
		? std::chrono::minutes(15)
		/* lower clear_interval for jailed (per-account?)
		   processes */
		: std::chrono::minutes(5);
}

StockRequest
MultiWasStock::PreserveRequest(StockRequest request) noexcept
{
	const auto &src = *reinterpret_cast<const CgiChildParams *>(request.get());
	return WithPoolDisposablePointer<CgiChildParams>::New(pool_new_linear(pool, "CgiChildParams", 4096), src);
}

bool
MultiWasStock::WantStderrPond(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;
	return params.options.stderr_pond;
}

std::string_view
MultiWasStock::GetChildTag(const void *info) const noexcept
{
	const auto &params = *(const CgiChildParams *)info;

	return params.options.tag;
}

std::unique_ptr<ChildStockItem>
MultiWasStock::CreateChild(CreateStockItem c,
			   const void *info,
			   ChildStock &_child_stock)
{
	return std::make_unique<MultiWasChild>(c, _child_stock,
					       GetChildTag(info));
}

void
MultiWasStock::PrepareChild(const void *info, PreparedChildProcess &p,
			    FdHolder &close_fds)
{
	const auto &params = *(const CgiChildParams *)info;

	p.Append(params.executable_path);
	for (auto i : params.args)
		p.Append(i);

	params.options.CopyTo(p, close_fds);
}

StockItem *
MultiWasStock::Create(CreateStockItem c, StockItem &shared_item)
{
	auto &child = (MultiWasChild &)shared_item;

	auto *connection = new MultiWasConnection(c, child);

#ifdef HAVE_URING
	if (uring_queue != nullptr)
		connection->EnableUring(*uring_queue);
#endif

	return connection;
}

void
MultiWasStock::FadeTag(std::string_view tag) noexcept
{
	mchild_stock.FadeIf([tag](const StockItem &item){
		const auto &child = (const MultiWasChild &)item;
		return child.IsTag(tag);
	});
}

void
MultiWasStock::Get(AllocatorPtr alloc,
		   StockKey key,
		   const ChildOptions &options,
		   const char *executable_path,
		   std::span<const char *const> args,
		   unsigned parallelism, unsigned concurrency,
		   StockGetHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept
{
	auto r = NewDisposablePointer<CgiChildParams>(alloc, executable_path,
						      args, options,
						      parallelism, concurrency,
						      false);

	mchild_stock.Get(key, std::move(r), concurrency, handler, cancel_ptr);
}
