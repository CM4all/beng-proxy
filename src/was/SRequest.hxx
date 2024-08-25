// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/LeakDetector.hxx"
#include "stock/GetHandler.hxx"
#include "http/PendingRequest.hxx"
#include "http/ResponseHandler.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

class WasMetricsHandler;

class WasStockRequest
	: protected StockGetHandler, HttpResponseHandler,
	  protected Cancellable,
	  PoolLeakDetector
{
protected:
	struct pool &pool;

public:
	StopwatchPtr stopwatch;

	const char *const site_name;

	const char *const remote_host;

	PendingHttpRequest pending_request;
	const char *const script_name;
	const char *const path_info;
	const char *const query_string;

	const std::span<const char *const> parameters;

	WasMetricsHandler *const metrics_handler;
	HttpResponseHandler &handler;
	CancellablePointer cancel_ptr;

public:
	WasStockRequest(struct pool &_pool,
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

	virtual ~WasStockRequest() noexcept = default;

protected:
	void Destroy() noexcept;

private:
	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept final;
	void OnStockItemError(std::exception_ptr ep) noexcept final;

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept final;
	void OnHttpError(std::exception_ptr error) noexcept final;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept final;
};
