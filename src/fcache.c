/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcache.h"
#include "cache.h"
#include "http-request.h"
#include "header-writer.h"
#include "strmap.h"
#include "http-response.h"
#include "date.h"
#include "strref.h"
#include "growing-buffer.h"
#include "abort-unref.h"
#include "tpool.h"
#include "http-util.h"
#include "get.h"
#include "resource-address.h"
#include "resource-loader.h"
#include "istream.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <event.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static const off_t cacheable_size_limit = 256 * 1024;

static const struct timeval fcache_timeout = { .tv_sec = 60 };

struct filter_cache {
    struct pool *pool;
    struct cache *cache;

    struct resource_loader *resource_loader;

    struct list_head requests;
};

struct filter_cache_info {
    /** when will the cached resource expire? (beng-proxy time) */
    time_t expires;

    /** the final resource id */
    const char *key;
};

struct filter_cache_item {
    struct cache_item item;

    struct pool *pool;

    struct filter_cache_info info;

    http_status_t status;
    struct strmap *headers;

    size_t size;
    unsigned char *data;
};

struct filter_cache_request {
    struct list_head siblings;

    struct pool *pool, *caller_pool;
    struct filter_cache *cache;
    struct http_response_handler_ref handler;

    struct filter_cache_info *info;

    struct {
        http_status_t status;
        struct strmap *headers;
        struct istream *input;
        size_t length;
        struct growing_buffer *output;
    } response;

    /**
     * This event is initialized by the response callback, and limits
     * the duration for receiving the response body.
     */
    struct event timeout;
};


static struct filter_cache_info *
filter_cache_info_new(struct pool *pool)
{
    struct filter_cache_info *info = p_malloc(pool, sizeof(*info));

    info->expires = (time_t)-1;
    return info;
}

/**
 * Release resources held by this request.
 */
static void
filter_cache_request_release(struct filter_cache_request *request)
{
    assert(request != NULL);
    assert(request->response.input == NULL);

    evtimer_del(&request->timeout);

    list_remove(&request->siblings);
    pool_unref(request->pool);
}

/**
 * Abort the request.
 */
static void
filter_cache_request_abort(struct filter_cache_request *request)
{
    assert(request != NULL);
    assert(request->response.input != NULL);

    istream_free_handler(&request->response.input);
    filter_cache_request_release(request);
}

/* check whether the request could produce a cacheable response */
static struct filter_cache_info *
filter_cache_request_evaluate(struct pool *pool,
                              const struct resource_address *address,
                              const char *source_id)
{
    struct filter_cache_info *info;

    if (source_id == NULL)
        return NULL;

    info = filter_cache_info_new(pool);
    info->key = p_strcat(pool, source_id, "|",
                         resource_address_id(address, pool), NULL);

    return info;
}

static void
filter_cache_info_copy(struct pool *pool, struct filter_cache_info *dest,
                       const struct filter_cache_info *src)
{
    dest->expires = src->expires;
    dest->key = p_strdup(pool, src->key);
}

static struct filter_cache_info *
filter_cache_info_dup(struct pool *pool, const struct filter_cache_info *src)
{
    struct filter_cache_info *dest = p_malloc(pool, sizeof(*dest));

    filter_cache_info_copy(pool, dest, src);
    return dest;
}

static struct filter_cache_request *
filter_cache_request_dup(struct pool *pool,
                         const struct filter_cache_request *src)
{
    struct filter_cache_request *dest = p_malloc(pool, sizeof(*dest));

    dest->pool = pool;
    dest->caller_pool = src->caller_pool;
    dest->cache = src->cache;
    dest->handler = src->handler;
    dest->info = filter_cache_info_dup(pool, src->info);
    return dest;
}

static void
filter_cache_put(struct filter_cache_request *request)
{
    time_t expires;
    struct pool *pool;
    struct filter_cache_item *item;

    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "filter_cache: put %s\n", request->info->key);

    if (request->info->expires == (time_t)-1)
        expires = time(NULL) + 3600;
    else
        expires = request->info->expires;

    pool = pool_new_linear(request->cache->pool, "filter_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));
    item->pool = pool;
    filter_cache_info_copy(pool, &item->info, request->info);

    item->status = request->response.status;
    item->headers = strmap_dup(pool, request->response.headers, 7);

    item->data = request->response.length > 0
        ? growing_buffer_dup(request->response.output, pool, &item->size)
        : NULL;

    cache_item_init(&item->item, expires, pool_size(pool));

    cache_put(request->cache->cache,
              item->info.key, &item->item);
}

static time_t
parse_translate_time(const char *p, time_t offset)
{
    time_t t;

    if (p == NULL)
        return (time_t)-1;

    t = http_date_parse(p);
    if (t != (time_t)-1)
        t += offset;

    return t;
}

/** check whether the HTTP response should be put into the cache */
static bool
filter_cache_response_evaluate(struct filter_cache_info *info,
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
    if (p != NULL && http_list_contains(p, "no-store"))
        return false;

    now = time(NULL);

    p = strmap_get(headers, "date");
    if (p != NULL) {
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
    struct filter_cache_request *request = ctx;

    /* reading the response has taken too long already; don't store
       this resource */
    cache_log(4, "filter_cache: timeout %s\n", request->info->key);
    filter_cache_request_abort(request);
}

/*
 * istream handler
 *
 */

static size_t
filter_cache_response_body_data(const void *data, size_t length, void *ctx)
{
    struct filter_cache_request *request = ctx;

    request->response.length += length;
    if (request->response.length > (size_t)cacheable_size_limit) {
        filter_cache_request_abort(request);
        return 0;
    }

    growing_buffer_write_buffer(request->response.output, data, length);
    return length;
}

static void
filter_cache_response_body_eof(void *ctx)
{
    struct filter_cache_request *request = ctx;

    request->response.input = NULL;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    filter_cache_put(request);

    filter_cache_request_release(request);
}

static void
filter_cache_response_body_abort(GError *error, void *ctx)
{
    struct filter_cache_request *request = ctx;

    cache_log(4, "filter_cache: body_abort %s: %s\n",
              request->info->key, error->message);
    g_error_free(error);

    request->response.input = NULL;

    filter_cache_request_release(request);
}

static const struct istream_handler filter_cache_response_body_handler = {
    .data = filter_cache_response_body_data,
    .eof = filter_cache_response_body_eof,
    .abort = filter_cache_response_body_abort,
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
    struct filter_cache_request *request = ctx;
    off_t available;
    struct pool *caller_pool = request->caller_pool;

    available = body == NULL ? 0 : istream_available(body, true);

    if (!filter_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "filter_cache: nocache %s\n", request->info->key);

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);
        pool_unref(caller_pool);
        return;
    }

    if (body == NULL) {
        request->response.output = NULL;
        filter_cache_put(request);
    } else {
        struct pool *pool;
        size_t buffer_size;

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
        request->response.length = 0;

        istream_assign_handler(&request->response.input,
                               istream_tee_second(body),
                               &filter_cache_response_body_handler, request,
                               0);

        if (available == (off_t)-1 || available < 256)
            buffer_size = 1024;
        else if (available > 16384)
            buffer_size = 16384;
        else
            buffer_size = (size_t)available;
        request->response.output = growing_buffer_new(request->pool,
                                                      buffer_size);

        pool_ref(request->pool);
        list_add(&request->siblings, &request->cache->requests);

        evtimer_set(&request->timeout, fcache_timeout_callback, request);
        evtimer_add(&request->timeout, &fcache_timeout);
    }

    http_response_handler_invoke_response(&request->handler, status,
                                          headers, body);
    pool_unref(caller_pool);

    if (body != NULL) {
        if (request->response.input != NULL)
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_read(request->response.input);

        pool_unref(request->pool);
    }
}

static void
filter_cache_response_abort(GError *error, void *ctx)
{
    struct filter_cache_request *request = ctx;

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
    struct filter_cache_item *item = (struct filter_cache_item *)_item;

    (void)item;
    return true;
}

static void
filter_cache_item_destroy(struct cache_item *_item)
{
    struct filter_cache_item *item = (struct filter_cache_item *)_item;

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
    pool = pool_new_libc(pool, "filter_cache");

    struct filter_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = cache_new(pool, &filter_cache_class, 65521, max_size);
    cache->resource_loader = resource_loader;
    list_init(&cache->requests);
    return cache;
}

static inline struct filter_cache_request *
list_head_to_request(struct list_head *head)
{
    return (struct filter_cache_request *)(((char*)head) - offsetof(struct filter_cache_request, siblings));
}

static void
filter_cache_request_close(struct filter_cache_request *request)
{
    assert(request != NULL);
    assert(request->response.input != NULL);
    assert(request->response.output != NULL);

    filter_cache_request_abort(request);
}

void
filter_cache_close(struct filter_cache *cache)
{
    while (!list_empty(&cache->requests)) {
        struct filter_cache_request *request =
            list_head_to_request(cache->requests.next);

        filter_cache_request_close(request);
    }

    cache_close(cache->cache);

    pool_unref(cache->pool);
}

void
filter_cache_get_stats(const struct filter_cache *cache,
                       struct cache_stats *data)
{
    cache_get_stats(cache->cache, data);
}

void
filter_cache_flush(struct filter_cache *cache)
{
    cache_flush(cache->cache);
}

static void
filter_cache_miss(struct filter_cache *cache, struct pool *caller_pool,
                  struct filter_cache_info *info,
                  const struct resource_address *address,
                  http_status_t status, struct strmap *headers,
                  struct istream *body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    struct pool *pool;
    struct filter_cache_request *request;

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "filter_cache_request", 8192);

    request = p_malloc(pool, sizeof(*request));
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
filter_cache_serve(struct filter_cache *cache, struct filter_cache_item *item,
                   struct pool *pool, struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    struct istream *response_body;

    if (body != NULL)
        istream_close_unused(body);

    cache_log(4, "filter_cache: serve %s\n", item->info.key);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    /* XXX hold reference on item */

    assert(item->data == NULL || item->item.size >= item->size);

    response_body = item->data != NULL
        ? istream_memory_new(pool, item->data, item->size)
        : istream_null_new(pool);

    response_body = istream_unlock_new(pool, response_body,
                                       cache->cache, &item->item);

    http_response_handler_invoke_response(&handler_ref, item->status,
                                          item->headers, response_body);
}

static void
filter_cache_found(struct filter_cache *cache,
                   struct filter_cache_item *item,
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
    struct filter_cache_info *info;

    info = filter_cache_request_evaluate(pool, address, source_id);
    if (info != NULL) {
        struct filter_cache_item *item
            = (struct filter_cache_item *)cache_get(cache->cache, info->key);

        if (item == NULL)
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
