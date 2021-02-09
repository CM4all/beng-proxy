/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "cache.hxx"
#include "SlicePool.hxx"
#include "rubber.hxx"
#include "http/Status.h"

#include <stddef.h>

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

public:
	HttpCacheHeap(struct pool &pool, EventLoop &event_loop,
		      size_t max_size) noexcept;

	Rubber &GetRubber() noexcept {
		return rubber;
	}

	void ForkCow(bool inherit) noexcept;

	[[gnu::pure]]
	AllocatorStats GetStats() const noexcept;

	HttpCacheDocument *Get(const char *uri,
			       StringMap &request_headers) noexcept;

	void Put(const char *url,
		 const HttpCacheResponseInfo &info,
		 StringMap &request_headers,
		 http_status_t status,
		 const StringMap &response_headers,
		 RubberAllocation &&a, size_t size) noexcept;

	void Remove(HttpCacheDocument &document) noexcept;
	void RemoveURL(const char *url, StringMap &headers) noexcept;

	void Compress() noexcept;
	void Flush() noexcept;

	static void Lock(HttpCacheDocument &document) noexcept;
	void Unlock(HttpCacheDocument &document) noexcept;

	UnusedIstreamPtr OpenStream(struct pool &_pool,
				    HttpCacheDocument &document) noexcept;
};
