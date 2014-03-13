/*
 * Caching HTTP responses in heap memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_heap.hxx"
#include "http_cache_internal.hxx"
#include "http_cache_age.hxx"
#include "cache.h"
#include "istream.h"
#include "rubber.h"
#include "istream_rubber.h"
#include "slice.h"

#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

struct http_cache_item {
    struct cache_item item;

    struct pool *pool;

    struct http_cache_document document;

    size_t size;

    struct rubber *rubber;
    unsigned rubber_id;
};

static inline struct http_cache_item *
document_to_item(struct http_cache_document *document)
{
    void *p = ((char *)document) - offsetof(struct http_cache_item, document);
        return (struct http_cache_item *)p;
}

static bool
http_cache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct http_cache_item *item =
        (const struct http_cache_item *)_item;
    struct strmap *headers = (struct strmap *)ctx;

    return http_cache_document_fits(&item->document, headers);
}

struct http_cache_document *
http_cache_heap_get(struct http_cache_heap *cache, const char *uri,
                    struct strmap *request_headers)
{
    struct http_cache_item *item
        = (struct http_cache_item *)cache_get_match(cache->cache, uri,
                                                    http_cache_item_match,
                                                    request_headers);
    if (item == nullptr)
        return nullptr;

    return &item->document;
}

void
http_cache_heap_put(struct http_cache_heap *cache,
                    const char *url,
                    const struct http_cache_info *info,
                    struct strmap *request_headers,
                    http_status_t status,
                    struct strmap *response_headers,
                    struct rubber *rubber, unsigned rubber_id, size_t size)
{

    struct pool *pool = pool_new_slice(cache->pool, "http_cache_item",
                                       cache->slice_pool);
    auto item = PoolAlloc<http_cache_item>(pool);

    item->pool = pool;

    http_cache_document_init(&item->document, pool, info,
                             request_headers, status, response_headers);
    item->size = size;
    item->rubber = rubber;
    item->rubber_id = rubber_id;

    cache_item_init_absolute(&item->item,
                             http_cache_calc_expires(info, request_headers),
                             pool_netto_size(pool) + item->size);

    cache_put_match(cache->cache, p_strdup(pool, url),
                    &item->item,
                    http_cache_item_match, request_headers);
}

void
http_cache_heap_remove(struct http_cache_heap *cache, const char *url,
                       struct http_cache_document *document)
{
    struct cache *cache2 = cache->cache;
    struct http_cache_item *item = document_to_item(document);

    cache_remove_item(cache2, url, &item->item);
    cache_item_unlock(cache2, &item->item);
}

void
http_cache_heap_remove_url(struct http_cache_heap *cache, const char *url,
                           struct strmap *headers)
{
    cache_remove_match(cache->cache, url,
                       http_cache_item_match, headers);
}

void
http_cache_heap_flush(struct http_cache_heap *cache)
{
    cache_flush(cache->cache);
    slice_pool_compress(cache->slice_pool);
}

void
http_cache_heap_lock(struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    cache_item_lock(&item->item);
}

void
http_cache_heap_unlock(struct http_cache_heap *cache,
                       struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    cache_item_unlock(cache->cache, &item->item);
}

struct istream *
http_cache_heap_istream(struct pool *pool, struct http_cache_heap *cache,
                        struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    if (item->rubber_id == 0)
        /* don't lock the item */
        return istream_null_new(pool);

    struct istream *istream =
        istream_rubber_new(pool, item->rubber, item->rubber_id,
                           0, item->size, false);
    return istream_unlock_new(pool, istream,
                              cache->cache, &item->item);
}


/*
 * cache_class
 *
 */

static bool
http_cache_item_validate(struct cache_item *_item)
{
    struct http_cache_item *item = (struct http_cache_item *)_item;

    (void)item;
    return true;
}

static void
http_cache_item_destroy(struct cache_item *_item)
{
    struct http_cache_item *item = (struct http_cache_item *)_item;

    if (item->rubber_id != 0)
        rubber_remove(item->rubber, item->rubber_id);

    pool_unref(item->pool);
}

static const struct cache_class http_cache_class = {
    .validate = http_cache_item_validate,
    .destroy = http_cache_item_destroy,
};


/*
 * cache_class
 *
 */

void
http_cache_heap_init(struct http_cache_heap *cache,
                     struct pool *pool, size_t max_size)
{
    cache->pool = pool;
    cache->cache = cache_new(pool, &http_cache_class, 65521, max_size);

    cache->slice_pool = slice_pool_new(1024, 65536);
}


void
http_cache_heap_deinit(struct http_cache_heap *cache)
{
    cache_close(cache->cache);
    slice_pool_free(cache->slice_pool);
}

void
http_cache_heap_get_stats(const struct http_cache_heap *cache,
                          const struct rubber *rubber,
                          struct cache_stats *data)
{
    cache_get_stats(cache->cache, data);

    data->netto_size += rubber_get_netto_size(rubber);
    data->brutto_size += rubber_get_brutto_size(rubber);
}
