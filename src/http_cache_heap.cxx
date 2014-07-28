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
#include "istream.h"
#include "rubber.hxx"
#include "istream_rubber.hxx"
#include "slice.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

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

    Rubber *rubber;
    unsigned rubber_id;

    http_cache_item(struct pool &_pool,
                    const struct http_cache_response_info &info,
                    const struct strmap *request_headers,
                    http_status_t status,
                    const struct strmap *response_headers,
                    size_t _size,
                    Rubber &_rubber, unsigned _rubber_id)
        :pool(&_pool),
         document(_pool, info, request_headers, status, response_headers),
         size(_size),
         rubber(&_rubber), rubber_id(_rubber_id) {

        cache_item_init_absolute(&item,
                                 http_cache_calc_expires(info, document.vary),
                                 pool_netto_size(pool) + size);
    }

    http_cache_item(const http_cache_item &) = delete;

    static http_cache_item *FromDocument(http_cache_document *document) {
        return &ContainerCast2(*document, &http_cache_item::document);
    }

    struct istream *OpenStream(struct pool *_pool) {
        return istream_rubber_new(_pool, rubber, rubber_id, 0, size, false);
    }
};

static bool
http_cache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct http_cache_item *item =
        (const struct http_cache_item *)_item;
    const struct strmap *headers = (const struct strmap *)ctx;

    return item->document.VaryFits(headers);
}

struct http_cache_document *
http_cache_heap::Get(const char *uri, struct strmap *request_headers)
{
    struct http_cache_item *item
        = (struct http_cache_item *)cache_get_match(cache, uri,
                                                    http_cache_item_match,
                                                    request_headers);
    if (item == nullptr)
        return nullptr;

    return &item->document;
}

void
http_cache_heap::Put(const char *url,
                     const struct http_cache_response_info &info,
                     struct strmap *request_headers,
                     http_status_t status,
                     const struct strmap *response_headers,
                     Rubber &rubber, unsigned rubber_id, size_t size)
{

    struct pool *item_pool = pool_new_slice(pool, "http_cache_item",
                                            slice_pool);
    auto item = NewFromPool<http_cache_item>(*item_pool, *item_pool,
                                             info, request_headers,
                                             status, response_headers,
                                             size, rubber, rubber_id);

    cache_put_match(cache, p_strdup(item_pool, url),
                    &item->item,
                    http_cache_item_match, request_headers);
}

void
http_cache_heap::Remove(const char *url, struct http_cache_document &document)
{
    auto item = http_cache_item::FromDocument(&document);

    cache_remove_item(cache, url, &item->item);
    cache_item_unlock(cache, &item->item);
}

void
http_cache_heap::RemoveURL(const char *url, struct strmap *headers)
{
    cache_remove_match(cache, url, http_cache_item_match, headers);
}

void
http_cache_heap::Flush()
{
    cache_flush(cache);
    slice_pool_compress(slice_pool);
}

void
http_cache_heap::Lock(struct http_cache_document &document)
{
    auto item = http_cache_item::FromDocument(&document);

    cache_item_lock(&item->item);
}

void
http_cache_heap::Unlock(struct http_cache_document &document)
{
    auto item = http_cache_item::FromDocument(&document);

    cache_item_unlock(cache, &item->item);
}

struct istream *
http_cache_heap::OpenStream(struct pool &_pool,
                            struct http_cache_document &document)
{
    auto item = http_cache_item::FromDocument(&document);

    if (item->rubber_id == 0)
        /* don't lock the item */
        return istream_null_new(&_pool);

    struct istream *istream = item->OpenStream(&_pool);
    return istream_unlock_new(&_pool, istream,
                              cache, &item->item);
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
http_cache_heap::Init(struct pool &_pool, size_t max_size)
{
    pool = &_pool;
    cache = cache_new(_pool, &http_cache_class, 65521, max_size);

    slice_pool = slice_pool_new(1024, 65536);
}


void
http_cache_heap::Deinit()
{
    cache_close(cache);
    slice_pool_free(slice_pool);
}

void
http_cache_heap::GetStats(const Rubber &rubber,
                          struct cache_stats &data) const
{
    cache_get_stats(cache, &data);

    data.netto_size += rubber_get_netto_size(&rubber);
    data.brutto_size += rubber_get_brutto_size(&rubber);
}
