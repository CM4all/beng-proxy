/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcache.h"
#include "cache.h"
#include "http_request.h"
#include "header-writer.h"
#include "strmap.h"
#include "http_response.h"
#include "date.h"
#include "strref.h"
#include "abort-unref.h"
#include "tpool.h"
#include "http_util.h"
#include "get.h"
#include "resource-address.h"
#include "resource-loader.h"
#include "istream.h"
#include "rubber.h"
#include "slice.h"
#include "istream_rubber.h"
#include "sink_rubber.h"
#include "istream_tee.h"
#include "async.h"
#include "cast.hxx"

#include <event.h>

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static constexpr off_t cacheable_size_limit = 256 * 1024;

static constexpr struct timeval fcache_timeout = { 60, 0 };

struct filter_cache {
    struct pool *pool;
    struct cache *cache;
    struct rubber *rubber;
    struct slice_pool *slice_pool;

    struct resource_loader *resource_loader;

    struct list_head requests;
};

struct filter_cache_info {
    /** when will the cached resource expire? (beng-proxy time) */
    time_t expires;

    /** the final resource id */
    const char *key;
};

struct FilterCacheItem {
    struct cache_item item;

    struct pool *pool;

    filter_cache_info info;

    http_status_t status;
    struct strmap *headers;

    size_t size;
    struct rubber *rubber;
    unsigned rubber_id;
};

struct FilterCacheRequest {
    struct list_head siblings;

    struct pool *pool, *caller_pool;
    struct filter_cache *cache;
    struct http_response_handler_ref handler;

    filter_cache_info *info;

    struct {
        http_status_t status;
        struct strmap *headers;

        /**
         * A handle to abort the sink_rubber that copies response body
         * data into a new rubber allocation.
         */
        struct async_operation_ref async_ref;
    } response;

    /**
     * This event is initialized by the response callback, and limits
     * the duration for receiving the response body.
     */
    struct event timeout;
};


static filter_cache_info *
filter_cache_info_new(struct pool *pool)
{
    filter_cache_info *info = NewFromPool<filter_cache_info>(pool);

    info->expires = (time_t)-1;
    return info;
}

/**
 * Release resources held by this request.
 */
static void
filter_cache_request_release(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(!async_ref_defined(&request->response.async_ref));

    evtimer_del(&request->timeout);

    list_remove(&request->siblings);
    pool_unref(request->pool);
}

/**
 * Abort the request.
 */
static void
filter_cache_request_abort(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(async_ref_defined(&request->response.async_ref));

    async_abort(&request->response.async_ref);
    async_ref_clear(&request->response.async_ref);
    filter_cache_request_release(request);
}

/* check whether the request could produce a cacheable response */
static filter_cache_info *
filter_cache_request_evaluate(struct pool *pool,
                              const struct resource_address *address,
                              const char *source_id)
{
    if (source_id == nullptr)
        return nullptr;

    filter_cache_info *info = filter_cache_info_new(pool);
    info->key = p_strcat(pool, source_id, "|",
                         resource_address_id(address, pool), nullptr);

    return info;
}

static void
filter_cache_info_copy(struct pool *pool, filter_cache_info *dest,
                       const filter_cache_info *src)
{
    dest->expires = src->expires;
    dest->key = p_strdup(pool, src->key);
}

static filter_cache_info *
filter_cache_info_dup(struct pool *pool, const filter_cache_info *src)
{
    filter_cache_info *dest = NewFromPool<filter_cache_info>(pool);

    filter_cache_info_copy(pool, dest, src);
    return dest;
}

static FilterCacheRequest *
filter_cache_request_dup(struct pool *pool,
                         const struct FilterCacheRequest *src)
{
    FilterCacheRequest *dest = NewFromPool<FilterCacheRequest>(pool);

    dest->pool = pool;
    dest->caller_pool = src->caller_pool;
    dest->cache = src->cache;
    dest->handler = src->handler;
    dest->info = filter_cache_info_dup(pool, src->info);
    return dest;
}

static void
filter_cache_put(FilterCacheRequest *request,
                 unsigned rubber_id, size_t size)
{
    assert(request != nullptr);
    assert(request->info != nullptr);

    cache_log(4, "filter_cache: put %s\n", request->info->key);

    time_t expires;
    if (request->info->expires == (time_t)-1)
        expires = time(nullptr) + 3600;
    else
        expires = request->info->expires;

    struct pool *pool = pool_new_slice(request->cache->pool, "FilterCacheItem",
                                       request->cache->slice_pool);
    FilterCacheItem *item = NewFromPool<FilterCacheItem>(pool);
    item->pool = pool;
    filter_cache_info_copy(pool, &item->info, request->info);

    item->status = request->response.status;
    item->headers = strmap_dup(pool, request->response.headers, 7);

    item->size = size;
    item->rubber = request->cache->rubber;
    item->rubber_id = rubber_id;

    cache_item_init_absolute(&item->item, expires,
                             pool_netto_size(pool) + item->size);

    cache_put(request->cache->cache,
              item->info.key, &item->item);
}

static time_t
parse_translate_time(const char *p, time_t offset)
{
    time_t t;

    if (p == nullptr)
        return (time_t)-1;

    t = http_date_parse(p);
    if (t != (time_t)-1)
        t += offset;

    return t;
}

/** check whether the HTTP response should be put into the cache */
static bool
filter_cache_response_evaluate(filter_cache_info *info,
                               http_status_t status, struct strmap *headers,
                               off_t body_available)
{
    time_t now, offset = 0;
    const char *p;

    if (status != HTTP_STATUS_OK)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    p = strmap_get(headers, "cache-control");
    if (p != nullptr && http_list_contains(p, "no-store"))
        return false;

    now = time(nullptr);

    p = strmap_get(headers, "date");
    if (p != nullptr) {
        time_t date = http_date_parse(p);
        if (date != (time_t)-1)
            offset = now - date;
    }

    if (info->expires == (time_t)-1) {
        info->expires = parse_translate_time(strmap_get(headers, "expires"), offset);
        if (info->expires != (time_t)-1 && info->expires < now)
            cache_log(2, "invalid 'expires' header\n");
    }

    /*
    info->out_etag = strmap_get(headers, "etag");
    */

    return true;
}

static void
fcache_timeout_callback(int fd gcc_unused, short event gcc_unused,
                        void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;

    /* reading the response has taken too long already; don't store
       this resource */
    cache_log(4, "filter_cache: timeout %s\n", request->info->key);
    filter_cache_request_abort(request);
}

/*
 * sink_rubber handler
 *
 */

static void
filter_cache_rubber_done(unsigned rubber_id, size_t size, void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;
    async_ref_clear(&request->response.async_ref);

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    filter_cache_put(request, rubber_id, size);

    filter_cache_request_release(request);
}

static void
filter_cache_rubber_no_store(void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;
    async_ref_clear(&request->response.async_ref);

    cache_log(4, "filter_cache: nocache %s\n", request->info->key);
    filter_cache_request_release(request);
}

static void
filter_cache_rubber_error(GError *error, void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;
    async_ref_clear(&request->response.async_ref);

    cache_log(4, "filter_cache: body_abort %s: %s\n",
              request->info->key, error->message);
    g_error_free(error);

    filter_cache_request_release(request);
}

static const struct sink_rubber_handler filter_cache_rubber_handler = {
    .done = filter_cache_rubber_done,
    .out_of_memory = filter_cache_rubber_no_store,
    .too_large = filter_cache_rubber_no_store,
    .error = filter_cache_rubber_error,
};

/*
 * http response handler
 *
 */

static void
filter_cache_response_response(http_status_t status, struct strmap *headers,
                               struct istream *body,
                               void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;
    off_t available;
    struct pool *caller_pool = request->caller_pool;

    available = body == nullptr ? 0 : istream_available(body, true);

    if (!filter_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "filter_cache: nocache %s\n", request->info->key);

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);
        pool_unref(caller_pool);
        return;
    }

    if (body == nullptr) {
        async_ref_clear(&request->response.async_ref);

        request->response.status = status;
        request->response.headers = headers;

        filter_cache_put(request, 0, 0);
    } else {
        struct pool *pool;

        /* move all this stuff to a new pool, so istream_tee's second
           head can continue to fill the cache even if our caller gave
           up on it */
        pool = pool_new_linear(request->cache->pool, "filter_cache_tee", 1024);
        request = filter_cache_request_dup(pool, request);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body, false, true);

        request->response.status = status;
        request->response.headers = strmap_dup(request->pool, headers, 17);

        pool_ref(request->pool);
        list_add(&request->siblings, &request->cache->requests);

        evtimer_set(&request->timeout, fcache_timeout_callback, request);
        evtimer_add(&request->timeout, &fcache_timeout);

        sink_rubber_new(pool, istream_tee_second(body),
                        request->cache->rubber, cacheable_size_limit,
                        &filter_cache_rubber_handler, request,
                        &request->response.async_ref);
    }

    http_response_handler_invoke_response(&request->handler, status,
                                          headers, body);
    pool_unref(caller_pool);

    if (body != nullptr) {
        if (async_ref_defined(&request->response.async_ref))
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_read(istream_tee_second(body));

        pool_unref(request->pool);
    }
}

static void
filter_cache_response_abort(GError *error, void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;

    g_prefix_error(&error, "http_cache %s: ", request->info->key);

    http_response_handler_invoke_abort(&request->handler, error);
    pool_unref(request->caller_pool);
}

static const struct http_response_handler filter_cache_response_handler = {
    .response = filter_cache_response_response,
    .abort = filter_cache_response_abort,
};


/*
 * cache_class
 *
 */

static bool
filter_cache_item_validate(struct cache_item *_item)
{
    FilterCacheItem *item = (FilterCacheItem *)_item;

    (void)item;
    return true;
}

static void
filter_cache_item_destroy(struct cache_item *_item)
{
    FilterCacheItem *item = (FilterCacheItem *)_item;

    if (item->rubber_id != 0)
        rubber_remove(item->rubber, item->rubber_id);

    pool_unref(item->pool);
}

static const struct cache_class filter_cache_class = {
    .validate = filter_cache_item_validate,
    .destroy = filter_cache_item_destroy,
};


/*
 * constructor and public methods
 *
 */

struct filter_cache *
filter_cache_new(struct pool *pool, size_t max_size,
                 struct resource_loader *resource_loader)
{
    if (max_size == 0) {
        /* the filter cache is disabled, return a nullptred object */
        filter_cache *cache = NewFromPool<filter_cache>(pool);
        cache->pool = pool;
        cache->cache = nullptr;
        cache->resource_loader = resource_loader;
        return cache;
    }

    pool = pool_new_libc(pool, "filter_cache");

    filter_cache *cache = NewFromPool<filter_cache>(pool);
    cache->pool = pool;

    if (max_size == 0) {
        /* the filter cache is disabled, return a nullptred object */

        cache->cache = nullptr;
        return cache;
    }

    /* leave 12.5% of the rubber allocator empty, to increase the
       chances that a hole can be found for a new allocation, to
       reduce the pressure that rubber_compress() creates */
    cache->cache = cache_new(pool, &filter_cache_class, 65521,
                             max_size * 7 / 8);

    cache->rubber = rubber_new(max_size);
    if (cache->rubber == nullptr) {
        fprintf(stderr, "Failed to allocate filter cache: %s\n",
                strerror(errno));
        _exit(2);
    }

    cache->slice_pool = slice_pool_new(1024, 65536);

    cache->resource_loader = resource_loader;
    list_init(&cache->requests);
    return cache;
}

static inline FilterCacheRequest *
list_head_to_request(struct list_head *head)
{
    return ContainerCast(head, FilterCacheRequest, siblings);
}

void
filter_cache_close(struct filter_cache *cache)
{
    if (cache->cache == nullptr) {
        /* filter cache is disabled */
        p_free(cache->pool, cache);
        return;
    }

    while (!list_empty(&cache->requests)) {
        FilterCacheRequest *request =
            list_head_to_request(cache->requests.next);

        filter_cache_request_abort(request);
    }

    cache_close(cache->cache);
    slice_pool_free(cache->slice_pool);
    rubber_free(cache->rubber);

    pool_unref(cache->pool);
}

void
filter_cache_fork_cow(struct filter_cache *cache, bool inherit)
{
    if (cache->cache == nullptr)
        return;

    rubber_fork_cow(cache->rubber, inherit);
}

void
filter_cache_get_stats(const struct filter_cache *cache,
                       struct cache_stats *data)
{
    if (cache->cache != nullptr)
        cache_get_stats(cache->cache, data);
    else
        /* filter cache is disabled */
        memset(data, 0, sizeof(*data));
}

void
filter_cache_flush(struct filter_cache *cache)
{
    if (cache->cache == nullptr)
        /* filter cache is disabled */
        return;

    cache_flush(cache->cache);
    rubber_compress(cache->rubber);
    slice_pool_compress(cache->slice_pool);
}

static void
filter_cache_miss(struct filter_cache *cache, struct pool *caller_pool,
                  filter_cache_info *info,
                  const struct resource_address *address,
                  http_status_t status, struct strmap *headers,
                  struct istream *body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    struct pool *pool;

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "filter_cache_request", 8192);

    FilterCacheRequest *request = NewFromPool<FilterCacheRequest>(pool);
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->info = info;

    cache_log(4, "filter_cache: miss %s\n", info->key);

    pool_ref(caller_pool);
    resource_loader_request(cache->resource_loader, pool, 0,
                            HTTP_METHOD_POST, address, status, headers, body,
                            &filter_cache_response_handler, request,
                            async_unref_on_abort(caller_pool, async_ref));
    pool_unref(pool);
}

static void
filter_cache_serve(struct filter_cache *cache, FilterCacheItem *item,
                   struct pool *pool, struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    struct istream *response_body;

    if (body != nullptr)
        istream_close_unused(body);

    cache_log(4, "filter_cache: serve %s\n", item->info.key);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    /* XXX hold reference on item */

    assert(item->rubber_id == 0 || item->item.size >= item->size);

    response_body = item->rubber_id != 0
        ? istream_rubber_new(pool, cache->rubber, item->rubber_id,
                             0, item->size, false)
        : istream_null_new(pool);

    response_body = istream_unlock_new(pool, response_body,
                                       cache->cache, &item->item);

    http_response_handler_invoke_response(&handler_ref, item->status,
                                          item->headers, response_body);
}

static void
filter_cache_found(struct filter_cache *cache,
                   FilterCacheItem *item,
                   struct pool *pool, struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx)
{
    filter_cache_serve(cache, item, pool, body, handler, handler_ctx);
}

void
filter_cache_request(struct filter_cache *cache,
                     struct pool *pool,
                     const struct resource_address *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers,
                     struct istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref)
{
    filter_cache_info *info = cache->cache != nullptr
        ? filter_cache_request_evaluate(pool, address, source_id)
        : nullptr;
    if (info != nullptr) {
        FilterCacheItem *item
            = (FilterCacheItem *)cache_get(cache->cache, info->key);

        if (item == nullptr)
            filter_cache_miss(cache, pool, info,
                              address, status, headers, body,
                              handler, handler_ctx, async_ref);
        else
            filter_cache_found(cache, item, pool, body,
                               handler, handler_ctx);
    } else {
        resource_loader_request(cache->resource_loader, pool, 0,
                                HTTP_METHOD_POST, address,
                                status, headers, body,
                                handler, handler_ctx, async_ref);
    }
}
