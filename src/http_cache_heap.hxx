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

#ifndef BENG_PROXY_HTTP_CACHE_HEAP_HXX
#define BENG_PROXY_HTTP_CACHE_HEAP_HXX

#include "http/Status.h"
#include "util/Compiler.h"

#include <stddef.h>

struct pool;
class UnusedIstreamPtr;
class Rubber;
class EventLoop;
class Cache;
class StringMap;
struct AllocatorStats;
struct HttpCacheResponseInfo;
struct SlicePool;
struct HttpCacheDocument;

/**
 * Caching HTTP responses in heap memory.
 */
class HttpCacheHeap {
    struct pool *const pool;

    Cache *const cache;

    SlicePool *const slice_pool;

public:
    HttpCacheHeap(struct pool &pool, EventLoop &event_loop,
                  size_t max_size) noexcept;
    ~HttpCacheHeap() noexcept;

    void ForkCow(bool inherit);

    gcc_pure
    AllocatorStats GetStats(const Rubber &rubber) const;

    HttpCacheDocument *Get(const char *uri, StringMap &request_headers);

    void Put(const char *url,
             const HttpCacheResponseInfo &info,
             StringMap &request_headers,
             http_status_t status,
             const StringMap &response_headers,
             Rubber &rubber, unsigned rubber_id, size_t size);

    void Remove(HttpCacheDocument &document);
    void RemoveURL(const char *url, StringMap &headers);

    void Compress();
    void Flush();

    static void Lock(HttpCacheDocument &document);
    void Unlock(HttpCacheDocument &document);

    UnusedIstreamPtr OpenStream(struct pool &_pool,
                                HttpCacheDocument &document);
};

#endif
