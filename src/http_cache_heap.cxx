/*
 * Caching HTTP responses in heap memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_heap.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_document.hxx"
#include "http_cache_age.hxx"
#include "cache.hxx"
#include "AllocatorStats.hxx"
#include "istream/istream.hxx"
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
                  const struct strmap *_request_headers,
                  http_status_t _status,
                  const struct strmap *_response_headers,
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

    Istream *OpenStream(struct pool &_pool) {
        return istream_rubber_new(_pool, *rubber, rubber_id, 0, size, false);
    }

    /* virtual methods from class CacheItem */
    void Destroy() override {
        if (rubber_id != 0)
            rubber_remove(rubber, rubber_id);

        pool_unref(pool);
    }
};

static bool
http_cache_item_match(const CacheItem *_item, void *ctx)
{
    const auto &item = *(const HttpCacheItem *)_item;
    const struct strmap *headers = (const struct strmap *)ctx;

    return item.VaryFits(headers);
}

HttpCacheDocument *
HttpCacheHeap::Get(const char *uri, struct strmap *request_headers)
{
    return (HttpCacheItem *)cache_get_match(cache, uri,
                                            http_cache_item_match,
                                            request_headers);
}

void
HttpCacheHeap::Put(const char *url,
                   const HttpCacheResponseInfo &info,
                   struct strmap *request_headers,
                   http_status_t status,
                   const struct strmap *response_headers,
                   Rubber &rubber, unsigned rubber_id, size_t size)
{
    struct pool *item_pool = pool_new_slice(pool, "http_cache_item",
                                            slice_pool);
    auto item = NewFromPool<HttpCacheItem>(*item_pool, *item_pool,
                                           info, request_headers,
                                           status, response_headers,
                                           size, rubber, rubber_id);

    cache_put_match(cache, p_strdup(item_pool, url),
                    item,
                    http_cache_item_match, request_headers);
}

void
HttpCacheHeap::Remove(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    cache_remove_item(cache, &item);
    cache_item_unlock(cache, &item);
}

void
HttpCacheHeap::RemoveURL(const char *url, struct strmap *headers)
{
    cache_remove_match(cache, url, http_cache_item_match, headers);
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
    cache_flush(cache);
    slice_pool_compress(slice_pool);
}

void
HttpCacheHeap::Lock(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    cache_item_lock(&item);
}

void
HttpCacheHeap::Unlock(HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    cache_item_unlock(cache, &item);
}

Istream *
HttpCacheHeap::OpenStream(struct pool &_pool, HttpCacheDocument &document)
{
    auto &item = (HttpCacheItem &)document;

    if (item.rubber_id == 0)
        /* don't lock the item */
        return istream_null_new(&_pool);

    Istream *istream = item.OpenStream(_pool);
    return istream_unlock_new(_pool, *istream,
                              *cache, item);
}

/*
 * cache_class
 *
 */

void
HttpCacheHeap::Init(struct pool &_pool, EventLoop &event_loop, size_t max_size)
{
    pool = &_pool;
    cache = cache_new(_pool, event_loop, 65521, max_size);

    slice_pool = slice_pool_new(1024, 65536);
}


void
HttpCacheHeap::Deinit()
{
    cache_close(cache);
    slice_pool_free(slice_pool);
}

AllocatorStats
HttpCacheHeap::GetStats(const Rubber &rubber) const
{
    return slice_pool_get_stats(*slice_pool) + rubber_get_stats(rubber);
}
