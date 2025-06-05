// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ResourceLoader.hxx"

class FilterCache;

/**
 * A #ResourceLoader implementation which sends HTTP requests through
 * the filter cache.
 */
class FilterResourceLoader final : public ResourceLoader {
	FilterCache &cache;

public:
	explicit FilterResourceLoader(FilterCache &_cache) noexcept
		:cache(_cache) {}

	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 const ResourceRequestParams &params,
			 HttpMethod method,
			 const ResourceAddress &address,
			 StringMap &&headers,
			 UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
