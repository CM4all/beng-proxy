// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Item.hxx"
#include "memory/SlicePool.hxx"
#include "memory/Rubber.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "cache.hxx"

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
	IntrusiveHashSet<HttpCacheItem, 65521,
			 HttpCacheItem::TagHash, HttpCacheItem::TagEqual,
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

	HttpCacheDocument *Get(const char *uri,
			       StringMap &request_headers) noexcept;

	void Put(const char *url, const char *tag,
		 const HttpCacheResponseInfo &info,
		 const StringMap &request_headers,
		 HttpStatus status,
		 const StringMap &response_headers,
		 RubberAllocation &&a, size_t size) noexcept;

	void Remove(HttpCacheDocument &document) noexcept;
	void RemoveURL(const char *url, StringMap &headers) noexcept;

	void Compress() noexcept;
	void Flush() noexcept;
	void FlushTag(std::string_view tag) noexcept;

	static void Lock(HttpCacheDocument &document) noexcept;
	void Unlock(HttpCacheDocument &document) noexcept;

	UnusedIstreamPtr OpenStream(struct pool &_pool,
				    HttpCacheDocument &document) noexcept;
};
