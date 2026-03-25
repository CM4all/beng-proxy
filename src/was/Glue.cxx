// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
#include "Stock.hxx"
#include "SRequest.hxx"
#include "cgi/Address.hxx"
#include "cgi/ChildParams.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <cassert>

class WasRequest final : WasStockRequest {
	WasStock &was_stock;
	const CgiAddress &address;

public:
	WasRequest(struct pool &_pool, WasStock &_was_stock,
		   StopwatchPtr &&_stopwatch,
		   const char *_site_name,
		   const CgiAddress &_address,
		   const char *_remote_host,
		   bool _tls,
		   const char *_document_root,
		   HttpMethod _method, const char *_uri,
		   StringMap &&_headers,
		   UnusedIstreamPtr _body,
		   WasMetricsHandler *_metrics_handler,
		   ::HttpResponseHandler &_handler) noexcept
		:WasStockRequest(_pool, std::move(_stopwatch),
				 _site_name, _remote_host, _tls, _document_root,
				 _method, _uri,
				 _address.script_name, _address.path_info, _address.query_string,
				 std::move(_headers), std::move(_body),
				 _address.params.ToArray(_pool),
				 _metrics_handler, _handler),
		 was_stock(_was_stock),
		 address(_address) {}

	using WasStockRequest::WasStockRequest;

	void Start(CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;
		GetStockItem();
	}

protected:
	void GetStockItem() noexcept override {
		auto r = NewFromPool<CgiChildParams>(pool, address.GetAction(),
						     address.args.ToArray(pool),
						     address.options,
						     address.parallelism,
						     address.concurrency,
						     address.disposable);

		const TempPoolLease tpool;
		const auto key = address.GetChildId(*tpool);

		was_stock.Get(key, *r,
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
	    bool tls,
	    const char *document_root,
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
					       tls,
					       document_root,
					       method, uri,
					       std::move(headers),
					       std::move(body),
					       metrics_handler,
					       handler);
	request->Start(cancel_ptr);
}
