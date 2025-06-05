// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FilterResourceLoader.hxx"
#include "http/cache/FilterCache.hxx"
#include "istream/UnusedPtr.hxx"
#include "http/Method.hxx"

#include <utility>

void
FilterResourceLoader::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  const ResourceRequestParams &params,
				  [[maybe_unused]] HttpMethod method,
				  const ResourceAddress &address,
				  StringMap &&headers,
				  UnusedIstreamPtr body,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	assert(method == HttpMethod::POST);

	filter_cache_request(cache, pool, parent_stopwatch, params.cache_tag,
			     address, params.body_etag,
			     params.status, std::move(headers), std::move(body),
			     handler, cancel_ptr);
}
