// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Stock.hxx"
#include "SRequest.hxx"
#include "cgi/Address.hxx"
#include "pool/pool.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

class WasRequest final : WasStockRequest {
	WasStock &was_stock;
	const CgiAddress &address;
	const char *const action;
	const std::span<const char *const> args;

public:
	WasRequest(struct pool &_pool, WasStock &_was_stock,
		   StopwatchPtr &&_stopwatch,
		   const char *_site_name,
		   const CgiAddress &_address,
		   const char *_remote_host,
		   HttpMethod _method, const char *_uri,
		   StringMap &&_headers,
		   UnusedIstreamPtr _body,
		   WasMetricsHandler *_metrics_handler,
		   ::HttpResponseHandler &_handler) noexcept
		:WasStockRequest(_pool, std::move(_stopwatch),
				 _site_name, _remote_host,
				 _method, _uri,
				 _address.script_name, _address.path_info, _address.query_string,
				 std::move(_headers), std::move(_body),
				 _address.params.ToArray(_pool),
				 _metrics_handler, _handler),
		 was_stock(_was_stock),
		 address(_address),
		 action(address.action != nullptr ? address.action : address.path),
		 args(address.args.ToArray(pool)) {}

	using WasStockRequest::WasStockRequest;

	void Start(CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		GetStockItem();
	}

protected:
	void GetStockItem() noexcept override {
		was_stock.Get(pool,
			      address.options,
			      action, args,
			      address.parallelism, address.disposable,
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
GetComaClass(const ExpandableStringList &parameters)
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
		  const ExpandableStringList &parameters)
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
	    const CgiAddress &address,
	    const char *remote_host,
	    HttpMethod method,
	    StringMap &&headers, UnusedIstreamPtr body,
	    WasMetricsHandler *metrics_handler,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr)
{
	const char *uri = address.GetURI(pool);

	auto request = NewFromPool<WasRequest>(pool, pool, was_stock,
					       stopwatch_new_was(parent_stopwatch,
								 address.path, uri,
								 address.path_info,
								 address.params),
					       site_name,
					       address,
					       remote_host,
					       method, uri,
					       std::move(headers),
					       std::move(body),
					       metrics_handler,
					       handler);
	request->Start(cancel_ptr);
}
