// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CachedResourceLoader.hxx"
#include "http/cache/Public.hxx"
#include "istream/UnusedPtr.hxx"

#include <utility>

void
CachedResourceLoader::SendRequest(struct pool &pool,
				  const StopwatchPtr &parent_stopwatch,
				  const ResourceRequestParams &params,
				  HttpMethod method,
				  const ResourceAddress &address,
				  StringMap &&headers,
				  UnusedIstreamPtr body,
				  HttpResponseHandler &handler,
				  CancellablePointer &cancel_ptr) noexcept
{
	http_cache_request(cache, pool, parent_stopwatch, params,
			   method, address,
			   std::move(headers), std::move(body),
			   handler, cancel_ptr);
}
