// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Stock.hxx"
#include "Launch.hxx"
#include "SConnection.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "cgi/ChildParams.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "pool/DisposablePointer.hxx"
#include "util/StringList.hxx"

#include <cassert>
#include <string>

std::size_t
WasStock::WasStockMap::GetLimit(const void *request,
				std::size_t _limit) const noexcept
{
	auto &params = *(const CgiChildParams *)request;
	if (params.parallelism > 0)
		return params.parallelism;

	return _limit;
}

class WasChild final : public WasStockConnection, ExitListener {
	const std::string tag;

	ChildErrorLog log;

	std::unique_ptr<ChildProcessHandle> handle;

	SharedLease listen_stream_lease;

	const bool disposable;

public:
	explicit WasChild(CreateStockItem c,
			  ChildErrorLog &&_log,
			  WasProcess &&process,
			  std::string_view _tag, bool _disposable) noexcept
		:WasStockConnection(c, std::move(process)),
		 tag(_tag),
		 log(std::move(_log)),
		 handle(std::move(process.handle)),
		 listen_stream_lease(std::move(process.listen_stream_lease)),
		 disposable(_disposable)
	{
		handle->SetExitListener(*this);
	}

	~WasChild() noexcept override;

	bool IsTag(std::string_view other_tag) const noexcept {
		return StringListContains(tag, '\0', other_tag);
	}

	void SetSite(const char *_site) noexcept override {
		log.SetSite(_site);
	}

	void SetUri(const char *_uri) noexcept override {
		log.SetUri(_uri);
	}

private:
	/* virtual methods from class StockItem */
	bool Release() noexcept override {
		return WasStockConnection::Release() && !disposable;
	}

	/* virtual methods from class ExitListener */
	void OnChildProcessExit([[maybe_unused]] int status) noexcept override {
		assert(handle);
		handle.reset();
	}
};

/*
 * stock class
 *
 */

void
WasStock::FadeTag(std::string_view tag) noexcept
{
	stock.FadeIf([tag](const StockItem &item){
		const auto &child = (const WasChild &)item;
		return child.IsTag(tag);
	});
}

void
WasStock::Create(CreateStockItem c, StockRequest _request,
		 StockGetHandler &handler,
		 CancellablePointer &)
{
	auto &params = *(CgiChildParams *)_request.get();

	assert(params.executable_path != nullptr);

	ChildErrorLog log;
	auto process = was_launch(spawn_service, listen_stream_stock,
				  c.GetStockNameView(),
				  params.executable_path,
				  params.args,
				  params.options,
				  log.EnableClient(GetEventLoop(),
						   log_sink, log_options,
						   params.options.stderr_pond));

	auto *child = new WasChild(c, std::move(log), std::move(process), params.options.tag, params.disposable);

	/* invoke the CgiChildParams destructor before invoking the
	   callback, because the latter may destroy the pool */
	_request.reset();

#ifdef HAVE_URING
	if (uring_queue != nullptr)
		child->EnableUring(*uring_queue);
#endif

	child->InvokeCreateSuccess(handler);
}

WasChild::~WasChild() noexcept = default;

/*
 * interface
 *
 */

void
WasStock::Get(AllocatorPtr alloc,
	      StockKey key,
	      const ChildOptions &options,
	      const char *executable_path,
	      std::span<const char *const> args,
	      unsigned parallelism, bool disposable,
	      StockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept
{

	auto r = NewDisposablePointer<CgiChildParams>(alloc, executable_path,
						      args, options,
						      parallelism,
						      0,
						      disposable);

	stock.Get(key, std::move(r), handler, cancel_ptr);
}
