// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Item.hxx"
#include "cache/Cache.hxx"
#include "memory/SlicePool.hxx"
#include "memory/Rubber.hxx"
#include "util/IntrusiveHashSet.hxx"

#include <string>

#include <stddef.h>

enum class HttpStatus : uint_least16_t;
struct pool;
class UnusedIstreamPtr;
class EventLoop;
class StringMap;
struct AllocatorStats;
struct HttpCacheResponseInfo;
struct HttpCacheDocument;

/**
 * Caching HTTP responses in heap memory.
 */
class HttpCacheHeap {
	struct pool &pool;

	SlicePool slice_pool;

	Rubber rubber;

	Cache cache;

	/**
	 * Lookup table to speed up FlushTag().
	 */
	IntrusiveHashSet<HttpCacheItem, 65536,
			 IntrusiveHashSetOperators<HttpCacheItem,
						   HttpCacheItem::GetTagFunction,
						   HttpCacheItem::TagHash,
						   std::equal_to<std::string_view>>,
			 IntrusiveHashSetMemberHookTraits<&HttpCacheItem::per_tag_hook>> per_tag;

public:
	HttpCacheHeap(struct pool &pool, EventLoop &event_loop,
		      size_t max_size) noexcept;
	~HttpCacheHeap() noexcept;

	Rubber &GetRubber() noexcept {
		return rubber;
	}

	void ForkCow(bool inherit) noexcept;

	[[gnu::pure]]
	AllocatorStats GetStats() const noexcept;

	HttpCacheDocument *Get(StringWithHash key,
			       StringMap &request_headers) noexcept;

	void Put(StringWithHash key, const char *tag,
		 const HttpCacheResponseInfo &info,
		 const StringMap &request_headers,
		 HttpStatus status,
		 const StringMap &response_headers,
		 RubberAllocation &&a, size_t size) noexcept;

	void Remove(HttpCacheDocument &document) noexcept;
	void Remove(StringWithHash key, const StringMap &headers) noexcept;

	void Compress() noexcept;
	void Flush() noexcept;
	void FlushTag(std::string_view tag) noexcept;

	[[nodiscard]]
	static SharedLease Lock(HttpCacheDocument &document) noexcept;

	UnusedIstreamPtr OpenStream(struct pool &_pool,
				    HttpCacheDocument &document) noexcept;
};
