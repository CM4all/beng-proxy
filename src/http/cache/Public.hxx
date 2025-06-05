// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

enum class HttpMethod : uint_least8_t;
struct pool;
class StopwatchPtr;
struct ResourceRequestParams;
class UnusedIstreamPtr;
class EventLoop;
class ResourceLoader;
struct ResourceAddress;
class StringMap;
class HttpResponseHandler;
struct CacheStats;
class HttpCache;
class CancellablePointer;

/**
 * Caching HTTP responses.
 */
HttpCache *
http_cache_new(struct pool &pool, size_t max_size,
	       bool obey_no_cache,
	       EventLoop &event_loop,
	       ResourceLoader &resource_loader);

void
http_cache_close(HttpCache *cache) noexcept;

void
http_cache_fork_cow(HttpCache &cache, bool inherit) noexcept;

[[gnu::pure]]
CacheStats
http_cache_get_stats(const HttpCache &cache) noexcept;

void
http_cache_flush(HttpCache &cache) noexcept;

void
http_cache_flush_tag(HttpCache &cache, std::string_view tag) noexcept;

void
http_cache_request(HttpCache &cache,
		   struct pool &pool,
		   const StopwatchPtr &parent_stopwatch,
		   const ResourceRequestParams &params,
		   HttpMethod method,
		   const ResourceAddress &address,
		   StringMap &&headers, UnusedIstreamPtr body,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;
