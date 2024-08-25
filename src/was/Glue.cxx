// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Stock.hxx"
#include "SRequest.hxx"
#include "was/async/Socket.hxx"
#include "pool/pool.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <string.h>

class WasRequest final : WasStockRequest {
	WasStock &was_stock;
	const ChildOptions &options;
	const char *const action;
	const std::span<const char *const> args;
	const unsigned parallelism;
	const bool disposable;

public:
	WasRequest(struct pool &_pool, WasStock &_was_stock,
		   StopwatchPtr &&_stopwatch,
		   const char *_site_name,
		   const ChildOptions &_options,
		   const char *_action,
		   std::span<const char *const> _args,
		   unsigned _parallelism, bool _disposable,
		   const char *_remote_host,
		   HttpMethod _method, const char *_uri,
		   const char *_script_name, const char *_path_info,
		   const char *_query_string,
		   StringMap &&_headers,
		   UnusedIstreamPtr _body,
		   std::span<const char *const> _parameters,
		   WasMetricsHandler *_metrics_handler,
		   ::HttpResponseHandler &_handler) noexcept
		:WasStockRequest(_pool, std::move(_stopwatch),
				 _site_name, _remote_host,
				 _method, _uri,
				 _script_name, _path_info, _query_string,
				 std::move(_headers), std::move(_body),
				 _parameters, _metrics_handler, _handler),
		 was_stock(_was_stock),
		 options(_options),
		 action(_action), args(_args),
		 parallelism(_parallelism),
		 disposable(_disposable) {}

	using WasStockRequest::WasStockRequest;

	void Start(CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		GetStockItem();
	}

protected:
	void GetStockItem() noexcept override {
		was_stock.Get(pool,
			      options,
			      action, args,
			      parallelism, disposable,
			      *this, cancel_ptr);
	}
};

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

	auto request = NewFromPool<WasRequest>(pool, pool, was_stock,
					       stopwatch_new_was(parent_stopwatch,
								 path, uri,
								 path_info,
								 parameters),
					       site_name,
					       options, action, args,
					       parallelism, disposable,
					       remote_host,
					       method, uri, script_name,
					       path_info, query_string,
					       std::move(headers),
					       std::move(body),
					       parameters,
					       metrics_handler,
					       handler);
	request->Start(cancel_ptr);
}
