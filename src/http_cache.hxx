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

#include "cluster/StickyHash.hxx"
#include "http/Method.h"
#include "util/Compiler.h"

#include <stddef.h>

struct pool;
class StopwatchPtr;
class UnusedIstreamPtr;
class EventLoop;
class ResourceLoader;
struct ResourceAddress;
class StringMap;
class HttpResponseHandler;
struct AllocatorStats;
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

gcc_pure
AllocatorStats
http_cache_get_stats(const HttpCache &cache) noexcept;

void
http_cache_flush(HttpCache &cache) noexcept;

/**
 * @param sticky_hash a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_cache_request(HttpCache &cache,
		   struct pool &pool,
		   const StopwatchPtr &parent_stopwatch,
		   sticky_hash_t sticky_hash,
		   const char *cache_tag,
		   const char *site_name,
		   http_method_t method,
		   const ResourceAddress &address,
		   StringMap &&headers, UnusedIstreamPtr body,
		   HttpResponseHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;
