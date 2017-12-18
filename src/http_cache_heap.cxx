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

#include "http_cache_heap.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_document.hxx"
#include "http_cache_age.hxx"
#include "cache.hxx"
#include "AllocatorStats.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "istream_unlock.hxx"
#include "istream_rubber.hxx"
#include "rubber.hxx"
#include "SlicePool.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

struct HttpCacheItem final : HttpCacheDocument, CacheItem {
    struct pool *pool;

    size_t size;

    Rubber *rubber;
    unsigned rubber_id;

    HttpCacheItem(struct pool &_pool,
                  const HttpCacheResponseInfo &_info,
                  const StringMap &_request_headers,
                  http_status_t _status,
                  const StringMap &_response_headers,
                  size_t _size,
                  Rubber &_rubber, unsigned _rubber_id)
        :HttpCacheDocument(_pool, _info, _request_headers,
                           _status, _response_headers),
         CacheItem(http_cache_calc_expires(_info, vary),
                   pool_netto_size(&_pool) + _size),
         pool(&_pool),
         size(_size),
         rubber(&_rubber), rubber_id(_rubber_id) {
    }

    HttpCacheItem(const HttpCacheItem &) = delete;
    HttpCacheItem &operator=(const HttpCacheItem &) = delete;

    UnusedIstreamPtr OpenStream(struct pool &_pool) {
        return istream_rubber_new(_pool, *rubber, rubber_id, 0, size, false);
    }

    /* virtual methods from class CacheItem */
    void Destroy() override {
        if (rubber_id != 0)
            rubber->Remove(rubber_id);

        pool_unref(pool);
    }
};

static bool
http_cache_item_match(const CacheItem *_item, void *ctx)
{
    const auto &item = *(const HttpCacheItem *)_item;
    const StringMap *headers = (const StringMap *)ctx;

    return item.VaryFits(headers);
}

HttpCacheDocument *
HttpCacheHeap::Get(const char *uri, StringMap &request_headers)
{
    return (HttpCacheItem *)cache->GetMatch(uri,
                                            http_cache_item_match,
                                            &request_headers);
}

void
HttpCacheHeap::Put(const char *url,
                   const HttpCacheResponseInfo &info,
                   StringMap &request_headers,
                   http_status_t status,
                   const StringMap &response_headers,
                   Rubber &rubber, unsigned rubber_id, size_t size)
{
    struct pool *item_pool = pool_new_slice(pool, "http_cache_item",
                                            slice_pool);
    auto item = NewFromPool<HttpCacheItem>(*item_pool, *item_pool,
                                           info, request_headers,
                                           status, response_headers,
                                           size, rubber, rubber_id);

    cache->PutMatch(p_strdup(item_pool, url), *item,
                    http_cache_item_match, &request_headers);
}

void
HttpCacheHeap::Remove(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    cache->Remove(item);
    item.Unlock();
}

void
HttpCacheHeap::RemoveURL(const char *url, StringMap &headers)
{
    cache->RemoveMatch(url, http_cache_item_match, &headers);
}

void
HttpCacheHeap::ForkCow(bool inherit)
{
    slice_pool_fork_cow(*slice_pool, inherit);
}

void
HttpCacheHeap::Compress()
{
    slice_pool_compress(slice_pool);
}

void
HttpCacheHeap::Flush()
{
    cache->Flush();
    slice_pool_compress(slice_pool);
}

void
HttpCacheHeap::Lock(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    item.Lock();
}

void
HttpCacheHeap::Unlock(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    item.Unlock();
}

UnusedIstreamPtr
HttpCacheHeap::OpenStream(struct pool &_pool, HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    if (item.rubber_id == 0)
        /* don't lock the item */
        return istream_null_new(_pool);

    return istream_unlock_new(_pool, item.OpenStream(_pool), item);
}

/*
 * cache_class
 *
 */

void
HttpCacheHeap::Init(struct pool &_pool, EventLoop &event_loop, size_t max_size)
{
    pool = &_pool;
    cache = new Cache(event_loop, 65521, max_size);

    slice_pool = slice_pool_new(1024, 65536);
}


void
HttpCacheHeap::Deinit()
{
    delete cache;
    slice_pool_free(slice_pool);
}

AllocatorStats
HttpCacheHeap::GetStats(const Rubber &rubber) const
{
    return slice_pool_get_stats(*slice_pool) + rubber.GetStats();
}
