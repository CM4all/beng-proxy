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
public:
	using WasStockRequest::WasStockRequest;

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
