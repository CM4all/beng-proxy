/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef BENG_FILTER_CACHE_HXX
#define BENG_FILTER_CACHE_HXX

#include "util/Compiler.h"
#include <http/status.h>

struct pool;
class Istream;
class EventLoop;
class ResourceLoader;
struct ResourceAddress;
class StringMap;
class HttpResponseHandler;
struct AllocatorStats;
class FilterCache;
class CancellablePointer;

/**
 * Caching filter responses.
 */
FilterCache *
filter_cache_new(struct pool *pool, size_t max_size,
                 EventLoop &event_loop,
                 ResourceLoader &resource_loader);

void
filter_cache_close(FilterCache *cache);

void
filter_cache_fork_cow(FilterCache &cache, bool inherit);

gcc_pure
AllocatorStats
filter_cache_get_stats(const FilterCache &cache);

void
filter_cache_flush(FilterCache &cache);

/**
 * @param source_id uniquely identifies the source; NULL means disable
 * the cache
 * @param status a HTTP status code for filter protocols which do have
 * one
 */
void
filter_cache_request(FilterCache &cache,
                     struct pool &pool,
                     const ResourceAddress &address,
                     const char *source_id,
                     http_status_t status, StringMap &&headers,
                     Istream *body,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr);

#endif
