/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "cache.h"
#include "growing-buffer.h"

#include <time.h>

struct http_cache_item {
    struct cache_item item;

    pool_t pool;

    struct http_cache_document document;

    size_t size;
    unsigned char *data;
};

/**
 * This constant is added to each cache_item's response body size, to
 * account for the cost of the supplemental attributes (such as
 * headers).
 */
static const size_t http_cache_item_base_size = 1024;

static inline struct http_cache_item *
document_to_item(struct http_cache_document *document)
{
    return (struct http_cache_item *)(((char *)document) - offsetof(struct http_cache_item, document));
}

static bool
http_cache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct http_cache_item *item =
        (const struct http_cache_item *)_item;
    struct strmap *headers = ctx;

    return http_cache_document_fits(&item->document, headers);
}

struct http_cache_document *
http_cache_heap_get(struct cache *cache, const char *uri,
                    struct strmap *request_headers)
{
    struct http_cache_item *item
        = (struct http_cache_item *)cache_get_match(cache, uri,
                                                    http_cache_item_match,
                                                    request_headers);
    if (item == NULL)
        return NULL;

    return &item->document;
}

void
http_cache_heap_put(struct cache *cache, pool_t pool, const char *url,
                    const struct http_cache_info *info,
                    struct strmap *request_headers,
                    http_status_t status,
                    struct strmap *response_headers,
                    const struct growing_buffer *body)
{
    struct http_cache_item *item;
    time_t expires;

    pool = pool_new_linear(pool, "http_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));

    if (info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        expires = time(NULL) + 3600;
    else
        expires = info->expires;

    cache_item_init(&item->item, expires,
                    http_cache_item_base_size +
                    (body != NULL ? growing_buffer_size(body) : 0));

    item->pool = pool;

    http_cache_document_init(&item->document, pool, info,
                             request_headers, status, response_headers);
    item->data = body != NULL
        ? growing_buffer_dup(body, pool, &item->size)
        : NULL;

    cache_put_match(cache, p_strdup(pool, url),
                    &item->item,
                    http_cache_item_match, request_headers);
}

void
http_cache_heap_remove(struct cache *cache, const char *url,
                       struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    cache_remove_item(cache, url, &item->item);
    cache_item_unlock(cache, &item->item);
}

void
http_cache_heap_remove_url(struct cache *cache, const char *url,
                           struct strmap *headers)
{
    cache_remove_match(cache, url,
                       http_cache_item_match, headers);
}

void
http_cache_heap_flush(struct cache *cache)
{
    cache_flush(cache);
}

void
http_cache_heap_lock(struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    cache_item_lock(&item->item);
}

void
http_cache_heap_unlock(struct cache *cache,
                       struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);

    cache_item_unlock(cache, &item->item);
}

istream_t
http_cache_heap_istream(pool_t pool, struct cache *cache,
                        struct http_cache_document *document)
{
    struct http_cache_item *item = document_to_item(document);
    istream_t istream;

    if (item->size == 0)
        /* don't lock the item */
        return istream_null_new(pool);

    istream = istream_memory_new(pool, item->data, item->size);
    return istream_unlock_new(pool, istream,
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

struct cache *
http_cache_heap_new(pool_t pool, size_t max_size)
{
    return cache_new(pool, &http_cache_class, 65521, max_size);
}


void
http_cache_heap_free(struct cache *cache)
{
    cache_close(cache);
}
