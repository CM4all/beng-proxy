// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Stock.hxx"
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
#include "util/Cancellable.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <string.h>

class WasRequest final : StockGetHandler, Cancellable, HttpResponseHandler, PoolLeakDetector {
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
	WasRequest(struct pool &_pool,
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
		   HttpResponseHandler &_handler)
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
		DeleteFromPool(pool, this);
	}

	void Start(WasStock &was_stock, const ChildOptions &options,
		   const char *action, std::span<const char *const> args,
		   unsigned parallelism, bool disposable,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;

		was_stock.Get(pool,
			      options,
			      action, args,
			      parallelism, disposable,
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
WasRequest::OnStockItemReady(StockItem &item) noexcept
{
	auto &connection = static_cast<WasStockConnection &>(item);
	connection.SetSite(site_name);
	connection.SetUri(pending_request.uri);

	const auto &process = connection.GetSocket();
	auto &lease = *NewFromPool<WasStockLease>(pool, connection);

	was_client_request(pool, item.GetStock().GetEventLoop(),
			   std::move(stopwatch),
			   process.control,
			   process.input, process.output,
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
WasRequest::OnStockItemError(std::exception_ptr ep) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeError(ep);
}

void
WasRequest::OnHttpResponse(HttpStatus status, StringMap &&_headers,
			   UnusedIstreamPtr _body) noexcept
{
	auto &_handler = handler;
	Destroy();
	_handler.InvokeResponse(status, std::move(_headers), std::move(_body));
}

void
WasRequest::OnHttpError(std::exception_ptr error) noexcept
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
was_request(struct pool &pool, WasStock &was_stock,
	    const StopwatchPtr &parent_stopwatch,
	    const char *site_name,
	    const ChildOptions &options,
	    const char *action,
	    const char *path,
	    std::span<const char *const> args,
	    unsigned parallelism, bool disposable,
	    const char *remote_host,
	    HttpMethod method, const char *uri,
	    const char *script_name, const char *path_info,
	    const char *query_string,
	    StringMap &&headers, UnusedIstreamPtr body,
	    std::span<const char *const> parameters,
	    WasMetricsHandler *metrics_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr)
{
	if (action == nullptr)
		action = path;

	auto request = NewFromPool<WasRequest>(pool, pool,
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
	request->Start(was_stock, options, action, args,
		       parallelism, disposable, cancel_ptr);
}
