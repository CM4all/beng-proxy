// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <string_view>

enum class HttpStatus : uint_least16_t;
struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
class EventLoop;
class ResourceLoader;
struct ResourceAddress;
class StringMap;
class HttpResponseHandler;
struct CacheStats;
class FilterCache;
class CancellablePointer;
struct StringWithHash;

/**
 * Caching filter responses.
 */
FilterCache *
filter_cache_new(struct pool *pool, size_t max_size,
		 EventLoop &event_loop,
		 ResourceLoader &resource_loader);

void
filter_cache_close(FilterCache *cache) noexcept;

void
filter_cache_fork_cow(FilterCache &cache, bool inherit) noexcept;

[[gnu::pure]]
CacheStats
filter_cache_get_stats(const FilterCache &cache) noexcept;

void
filter_cache_flush(FilterCache &cache) noexcept;

void
filter_cache_flush_tag(FilterCache &cache, std::string_view tag) noexcept;

/**
 * @param source_id uniquely identifies the source; NULL means disable
 * the cache
 * @param status a HTTP status code for filter protocols which do have
 * one
 */
void
filter_cache_request(FilterCache &cache,
		     struct pool &pool,
		     const StopwatchPtr &parent_stopwatch,
		     const char *cache_tag,
		     const ResourceAddress &address,
		     StringWithHash source_id,
		     HttpStatus status, StringMap &&headers,
		     UnusedIstreamPtr body,
		     HttpResponseHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept;
