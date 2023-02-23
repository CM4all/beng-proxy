// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FilterResourceLoader.hxx"
#include "fcache.hxx"
#include "istream/UnusedPtr.hxx"
#include "http/Method.hxx"

#include <utility>

void
FilterResourceLoader::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  const ResourceRequestParams &params,
				  [[maybe_unused]] HttpMethod method,
				  const ResourceAddress &address,
				  HttpStatus status,
				  StringMap &&headers,
				  UnusedIstreamPtr body,
				  const char *body_etag,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	assert(method == HttpMethod::POST);

	filter_cache_request(cache, pool, parent_stopwatch, params.cache_tag,
			     address, body_etag,
			     status, std::move(headers), std::move(body),
			     handler, cancel_ptr);
}
