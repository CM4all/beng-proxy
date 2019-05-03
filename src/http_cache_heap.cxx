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
#include "AllocatorStats.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "istream_unlock.hxx"
#include "istream_rubber.hxx"
#include "rubber.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"

struct HttpCacheItem final : PoolHolder, HttpCacheDocument, CacheItem {
    size_t size;

    const RubberAllocation body;

    HttpCacheItem(PoolPtr &&_pool,
                  std::chrono::steady_clock::time_point now,
                  std::chrono::system_clock::time_point system_now,
                  const HttpCacheResponseInfo &_info,
                  const StringMap &_request_headers,
                  http_status_t _status,
                  const StringMap &_response_headers,
                  size_t _size,
                  RubberAllocation &&_body) noexcept
        :PoolHolder(std::move(_pool)),
         HttpCacheDocument(pool, _info, _request_headers,
                           _status, _response_headers),
         CacheItem(now, system_now, http_cache_calc_expires(_info, vary),
                   pool_netto_size(pool) + _size),
         size(_size),
         body(std::move(_body)) {
    }

    HttpCacheItem(const HttpCacheItem &) = delete;
    HttpCacheItem &operator=(const HttpCacheItem &) = delete;

    using PoolHolder::GetPool;

    UnusedIstreamPtr OpenStream(struct pool &_pool) noexcept {
        return istream_rubber_new(_pool, body.GetRubber(), body.GetId(),
                                  0, size, false);
    }

    /* virtual methods from class CacheItem */
    void Destroy() noexcept override {
        pool_trash(pool);
        this->~HttpCacheItem();
    }
};

static bool
http_cache_item_match(const CacheItem *_item, void *ctx) noexcept
{
    const auto &item = *(const HttpCacheItem *)_item;
    const StringMap *headers = (const StringMap *)ctx;

    return item.VaryFits(headers);
}

HttpCacheDocument *
HttpCacheHeap::Get(const char *uri, StringMap &request_headers) noexcept
{
    return (HttpCacheItem *)cache.GetMatch(uri,
                                           http_cache_item_match,
                                           &request_headers);
}

void
HttpCacheHeap::Put(const char *url,
                   const HttpCacheResponseInfo &info,
                   StringMap &request_headers,
                   http_status_t status,
                   const StringMap &response_headers,
                   RubberAllocation &&a, size_t size) noexcept
{
    auto item = NewFromPool<HttpCacheItem>(pool_new_slice(&pool, "http_cache_item", &slice_pool),
                                           cache.SteadyNow(),
                                           cache.SystemNow(),
                                           info, request_headers,
                                           status, response_headers,
                                           size,
                                           std::move(a));

    cache.PutMatch(p_strdup(&item->GetPool(), url), *item,
                   http_cache_item_match, &request_headers);
}

void
HttpCacheHeap::Remove(HttpCacheDocument &document) noexcept
{
    auto &item = (HttpCacheItem &)document;

    cache.Remove(item);
    item.Unlock();
}

void
HttpCacheHeap::RemoveURL(const char *url, StringMap &headers) noexcept
{
    cache.RemoveMatch(url, http_cache_item_match, &headers);
}

void
HttpCacheHeap::ForkCow(bool inherit) noexcept
{
    slice_pool.ForkCow(inherit);
    rubber.ForkCow(inherit);
}

void
HttpCacheHeap::Compress() noexcept
{
    slice_pool.Compress();
    rubber.Compress();
}

void
HttpCacheHeap::Flush() noexcept
{
    cache.Flush();
    slice_pool.Compress();
    rubber.Compress();
}

void
HttpCacheHeap::Lock(HttpCacheDocument &document) noexcept
{
    auto &item = (HttpCacheItem &)document;

    item.Lock();
}

void
HttpCacheHeap::Unlock(HttpCacheDocument &document) noexcept
{
    auto &item = (HttpCacheItem &)document;

    item.Unlock();
}

UnusedIstreamPtr
HttpCacheHeap::OpenStream(struct pool &_pool,
                          HttpCacheDocument &document) noexcept
{
    auto &item = (HttpCacheItem &)document;

    if (!item.body)
        /* don't lock the item */
        return istream_null_new(_pool);

    return istream_unlock_new(_pool, item.OpenStream(_pool), item);
}

/*
 * cache_class
 *
 */

HttpCacheHeap::HttpCacheHeap(struct pool &_pool, EventLoop &event_loop,
                             size_t max_size) noexcept
    :pool(_pool),
     slice_pool(1024, 65536),
     rubber(max_size),
     /* leave 12.5% of the rubber allocator empty, to increase the
        chances that a hole can be found for a new allocation, to
        reduce the pressure that rubber_compress() creates */
     cache(event_loop, 65521, max_size * 7 / 8)
{
}

AllocatorStats
HttpCacheHeap::GetStats() const noexcept
{
    return slice_pool.GetStats() + rubber.GetStats();
}
