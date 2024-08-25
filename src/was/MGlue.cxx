// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "MGlue.hxx"
#include "MStock.hxx"
#include "RStock.hxx"
#include "SConnection.hxx"
#include "SLease.hxx"
#include "Client.hxx"
#include "was/async/Socket.hxx"
#include "http/PendingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Stock.hxx"
#include "stock/Item.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "stopwatch.hxx"
#include "net/SocketAddress.hxx"
#include "net/FormatAddress.hxx"
#include "util/Cancellable.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

class MultiWasRequest final
	: StockGetHandler, Cancellable, HttpResponseHandler, PoolLeakDetector
{
	struct pool &pool;

	StopwatchPtr stopwatch;

	const char *const site_name;

	const char *const remote_host;

	PendingHttpRequest pending_request;
	const char *script_name;
	const char *path_info;
	const char *query_string;

	std::span<const char *const> parameters;

	WasMetricsHandler *const metrics_handler;
	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	MultiWasRequest(struct pool &_pool,
			StopwatchPtr &&_stopwatch,
			const char *_site_name,
			const char *_remote_host,
			HttpMethod _method, const char *_uri,
			const char *_script_name, const char *_path_info,
			const char *_query_string,
			StringMap &&_headers,
			UnusedIstreamPtr _body,
			std::span<const char *const> _parameters,
			WasMetricsHandler *_metrics_handler,
			HttpResponseHandler &_handler) noexcept
		:PoolLeakDetector(_pool),
		 pool(_pool),
		 stopwatch(std::move(_stopwatch)),
		 site_name(_site_name),
		 remote_host(_remote_host),
		 pending_request(_pool, _method, _uri,
				 std::move(_headers), std::move(_body)),
		 script_name(_script_name),
		 path_info(_path_info), query_string(_query_string),
		 parameters(_parameters),
		 metrics_handler(_metrics_handler),
		 handler(_handler) {}

	void Destroy() noexcept {
		this->~MultiWasRequest();
	}

	void Start(MultiWasStock &stock, const ChildOptions &options,
		   const char *action, std::span<const char *const> args,
		   unsigned parallelism, unsigned concurrency,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		stock.Get(pool,
			  options,
			  action, args,
			  parallelism, concurrency,
			  *this, cancel_ptr);
	}

	void Start(RemoteWasStock &stock, SocketAddress address,
		   unsigned parallelism, unsigned concurrency,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		stock.Get(pool, address, parallelism, concurrency,
			  *this, cancel_ptr);
	}

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr error) noexcept override;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		auto c = std::move(cancel_ptr);
		Destroy();
		c.Cancel();
	}
};

/*
 * stock callback
 *
 */

void
MultiWasRequest::OnStockItemReady(StockItem &item) noexcept
{
	auto &connection = static_cast<WasStockConnection &>(item);
	connection.SetSite(site_name);
	connection.SetUri(pending_request.uri);

	const auto &socket = connection.GetSocket();
	auto &lease = *NewFromPool<WasStockLease>(pool, connection);

	was_client_request(pool, item.GetStock().GetEventLoop(),
			   std::move(stopwatch),
			   socket.control,
			   socket.input, socket.output,
			   lease,
			   remote_host,
			   pending_request.method, pending_request.uri,
			   script_name, path_info,
			   query_string,
			   pending_request.headers,
			   std::move(pending_request.body),
			   parameters,
			   metrics_handler,
			   *this, cancel_ptr);
}

void
MultiWasRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

void
MultiWasRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			        UnusedIstreamPtr _body) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
MultiWasRequest::OnHttpError(std::exception_ptr error) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(std::move(error));
}

/*
 * constructor
 *
 */

#ifdef ENABLE_STOPWATCH

[[gnu::pure]]
static const char *
GetComaClass(std::span<const char *const> parameters)
{
	for (const char *i : parameters) {
		const char *result = StringAfterPrefix(i, "COMA_CLASS=");
		if (result != nullptr && *result != 0)
			return result;
	}

	return nullptr;
}

#endif

static StopwatchPtr
stopwatch_new_was(const StopwatchPtr &parent_stopwatch,
		  const char *path, const char *uri,
		  const char *path_info,
		  std::span<const char *const> parameters)
{
#ifdef ENABLE_STOPWATCH
	assert(path != nullptr);
	assert(uri != nullptr);

	if (!stopwatch_is_enabled())
		return nullptr;

	/* special case for a very common COMA application */
	const char *coma_class = GetComaClass(parameters);
	if (coma_class != nullptr)
		path = coma_class;

	const char *slash = strrchr(path, '/');
	if (slash != nullptr && slash[1] != 0)
		path = slash + 1;

	if (path_info != nullptr && *path_info != 0)
		uri = path_info;

	std::string name = path;
	name.push_back(' ');
	name += uri;

	return StopwatchPtr(parent_stopwatch, name.c_str());
#else
	(void)parent_stopwatch;
	(void)path;
	(void)uri;
	(void)path_info;
	(void)parameters;
	return nullptr;
#endif
}

void
SendMultiWasRequest(struct pool &pool, MultiWasStock &stock,
		    const StopwatchPtr &parent_stopwatch,
		    const char *site_name,
		    const ChildOptions &options,
		    const char *action,
		    const char *path,
		    std::span<const char *const> args,
		    unsigned parallelism,
		    const char *remote_host,
		    HttpMethod method, const char *uri,
		    const char *script_name, const char *path_info,
		    const char *query_string,
		    StringMap &&headers, UnusedIstreamPtr body,
		    std::span<const char *const> parameters,
		    unsigned concurrency,
		    WasMetricsHandler *metrics_handler,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept
{
	if (action == nullptr)
		action = path;

	auto request = NewFromPool<MultiWasRequest>(pool, pool,
						    stopwatch_new_was(parent_stopwatch,
								      path, uri,
								      path_info,
								      parameters),
						    site_name,
						    remote_host,
						    method, uri, script_name,
						    path_info, query_string,
						    std::move(headers),
						    std::move(body),
						    parameters,
						    metrics_handler,
						    handler);
	request->Start(stock, options, action, args, parallelism, concurrency, cancel_ptr);
}

static StopwatchPtr
stopwatch_new_was(const StopwatchPtr &parent_stopwatch,
		  SocketAddress address, const char *uri,
		  const char *path_info,
		  std::span<const char *const> parameters)
{
#ifdef ENABLE_STOPWATCH
	assert(!address.IsNull());
	assert(address.IsDefined());
	assert(uri != nullptr);

	if (!stopwatch_is_enabled())
		return nullptr;

	const char *path = nullptr;

	/* special case for a very common COMA application */
	const char *coma_class = GetComaClass(parameters);
	if (coma_class != nullptr)
		path = coma_class;

	char path_buffer[1024];
	if (path == nullptr) {
		if (!ToString(path_buffer, address))
			return nullptr;
		path = path_buffer;
	}

	const char *slash = strrchr(path, '/');
	if (slash != nullptr && slash[1] != 0)
		path = slash + 1;

	if (path_info != nullptr && *path_info != 0)
		uri = path_info;

	std::string name = path;
	name.push_back(' ');
	name += uri;

	return StopwatchPtr(parent_stopwatch, name.c_str());
#else
	(void)parent_stopwatch;
	(void)address;
	(void)uri;
	(void)path_info;
	(void)parameters;
	return nullptr;
#endif
}

void
SendRemoteWasRequest(struct pool &pool, RemoteWasStock &stock,
		     const StopwatchPtr &parent_stopwatch,
		     SocketAddress address,
		     unsigned parallelism,
		     const char *remote_host,
		     HttpMethod method, const char *uri,
		     const char *script_name, const char *path_info,
		     const char *query_string,
		     StringMap &&headers, UnusedIstreamPtr body,
		     std::span<const char *const> parameters,
		     unsigned concurrency,
		     WasMetricsHandler *metrics_handler,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept
{
	auto request = NewFromPool<MultiWasRequest>(pool, pool,
						    stopwatch_new_was(parent_stopwatch,
								      address, uri,
								      path_info,
								      parameters),
						    nullptr,
						    remote_host,
						    method, uri, script_name,
						    path_info, query_string,
						    std::move(headers),
						    std::move(body),
						    parameters,
						    metrics_handler,
						    handler);
	request->Start(stock, address, parallelism, concurrency, cancel_ptr);
}
