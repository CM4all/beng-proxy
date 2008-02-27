/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache.h"
#include "cache.h"
#include "url-stream.h"
#include "header-writer.h"
#include "strmap.h"
#include "http-response.h"
#include "date.h"

#include <string.h>
#include <time.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

struct http_cache {
    pool_t pool;
    struct cache *cache;
    struct hstock *stock;
};

struct http_cache_info {
    /** when will the cached resource expire? (beng-proxy time) */
    time_t expires;

    /** when was the cached resource last modified on the widget
        server? (widget server time) */
    const char *last_modified;

    const char *etag;
};

struct http_cache_item {
    struct cache_item item;

    pool_t pool;

    struct http_cache_info info;

    http_status_t status;
    strmap_t headers;
    size_t length;
    unsigned char *data;
};

struct http_cache_request {
    pool_t pool;
    struct http_cache *cache;
    const char *url;
    struct http_response_handler_ref handler;

    struct http_cache_item *item;
    struct http_cache_info *info;

    http_status_t status;
    strmap_t headers;
    istream_t input;
    size_t length;
    struct growing_buffer *output;
};


/* check whether the request could produce a cacheable response */
static struct http_cache_info *
http_cache_request_evaluate(pool_t pool,
                            http_method_t method,
                            struct strmap *headers,
                            istream_t body)
{
    struct http_cache_info *info;
    const char *p;

    if (method != HTTP_METHOD_GET || body != NULL)
        return NULL;

    if (headers != NULL) {
        p = strmap_get(headers, "cache-control");
        if (p != NULL) {
            if (strcmp(p, "no-cache") == 0)
                return NULL;
        } else {
            p = strmap_get(headers, "pragma");
            if (p != NULL && strcmp(p, "no-cache") == 0)
                return NULL;
        }
    }

    info = p_malloc(pool, sizeof(*info));
    info->expires = (time_t)-1;
    info->last_modified = NULL;
    info->etag = NULL;
    return info;
}

static void
http_cache_copy_info(pool_t pool, struct http_cache_info *dest,
                     const struct http_cache_info *src)
{
    dest->expires = src->expires;

    if (src->last_modified != NULL)
        dest->last_modified = p_strdup(pool, src->last_modified);

    if (src->etag != NULL)
        dest->etag = p_strdup(pool, src->etag);
}

static void
http_cache_put(struct http_cache_request *request)
{
    pool_t pool;
    struct http_cache_item *item;

    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "http_cache: put %s\n", request->url);

    pool = pool_new_linear(request->cache->pool, "http_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));
    if (request->info->expires == (time_t)-1)
        item->item.expires = time(NULL) + 300; /* XXX 5 minutes */
    else
        item->item.expires = request->info->expires;
    item->pool = pool;
    http_cache_copy_info(pool, &item->info, request->info);
    item->status = request->status;
    item->headers = strmap_dup(pool, request->headers);
    item->length = request->length;

    if (item->length == 0) {
        item->data = NULL;
    } else {
        unsigned char *dest;
        const void *src;
        size_t length;

        item->data = dest = p_malloc(pool, item->length);
        while ((src = growing_buffer_read(request->output, &length)) != NULL) {
            memcpy(dest, src, length);
            dest += length;
            growing_buffer_consume(request->output, length);
        }
    }

    cache_put(request->cache->cache, p_strdup(pool, request->url), &item->item);
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
static int
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, strmap_t headers,
                             off_t body_available)
{
    time_t date, now, offset;
    const char *p;

    (void)headers;

    if (status != HTTP_STATUS_OK || body_available == 0)
        return 0;

    if (body_available != (off_t)-1 && body_available > 256 * 1024)
        /* too large for the cache */
        return 0;

    /* XXX cache-control */

    p = strmap_get(headers, "date");
    if (p == NULL)
        /* we cannot determine wether to cache a resource if the
           server does not provide its system time */
        return 0;
    date = http_date_parse(p);
    if (date == (time_t)-1)
        return 0;

    now = time(NULL);
    offset = now - date;

    info->expires = parse_translate_time(strmap_get(headers, "expires"), offset);
    if (info->expires != (time_t)-1 && info->expires < now)
        cache_log(2, "invalid 'expires' header\n");

    info->last_modified = strmap_get(headers, "last-modified");
    info->etag = strmap_get(headers, "etag");

    return info->expires != (time_t)-1 || info->last_modified != NULL;
}

static void
http_cache_serve(struct http_cache_item *item,
                 pool_t pool,
                 const char *url, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx);


/*
 * istream handler
 *
 */

static size_t
http_cache_response_body_data(const void *data, size_t length, void *ctx)
{
    struct http_cache_request *request = ctx;

    /* XXX second too-large-check */

    growing_buffer_write_buffer(request->output, data, length);
    request->length += length;
    return length;
}

static void
http_cache_response_body_eof(void *ctx)
{
    struct http_cache_request *request = ctx;

    http_cache_put(request);
    istream_clear_unref(&request->input);
}

static void
http_cache_response_body_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: body_abort %s\n", request->url);

    istream_clear_unref(&request->input);
}

static const struct istream_handler http_cache_response_body_handler = {
    .data = http_cache_response_body_data,
    .eof = http_cache_response_body_eof,
    .abort = http_cache_response_body_abort,
};


/*
 * http response handler
 *
 */

static void 
http_cache_response_response(http_status_t status, strmap_t headers,
                             istream_t body,
                             void *ctx)
{
    struct http_cache_request *request = ctx;
    off_t available;

    if (request->item != NULL && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == NULL);

        cache_log(5, "http_cache: not_modified %s\n", request->url);
        http_cache_serve(request->item, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        return;
    }

    available = body == NULL ? 0 : istream_available(body, 1);

    if (!http_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", request->url);

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);
        return;
    }

    request->length = 0;

    if (body == NULL) {
        request->output = NULL;
        http_cache_put(request);
    } else {
        size_t buffer_size;

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body);

        request->status = status;
        request->headers = headers;
        istream_assign_ref_handler(&request->input, istream_tee_second(body),
                                   &http_cache_response_body_handler, request,
                                   0);

        if (available == (off_t)-1 || available < 256)
            buffer_size = 1024;
        else if (available > 16384)
            buffer_size = 16384;
        else
            buffer_size = (size_t)available;
        request->output = growing_buffer_new(request->pool, buffer_size);
    }

    http_response_handler_invoke_response(&request->handler, status,
                                          headers, body);
}

static void 
http_cache_response_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: response_abort %s\n", request->url);

    http_response_handler_invoke_abort(&request->handler);
}

static const struct http_response_handler http_cache_response_handler = {
    .response = http_cache_response_response,
    .abort = http_cache_response_abort,
};


/*
 * cache_class
 *
 */

static int
http_cache_item_validate(struct cache_item *_item)
{
    struct http_cache_item *item = (struct http_cache_item *)_item;

    (void)item;
    return 1;
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
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(pool_t pool,
               struct hstock *http_client_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = cache_new(pool, &http_cache_class);
    cache->stock = http_client_stock;
    return cache;
}

void
http_cache_close(struct http_cache *cache)
{
    cache_close(cache->cache);
}

static void
http_cache_miss(struct http_cache *cache, struct http_cache_info *info,
                pool_t pool,
                http_method_t method, const char *url,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    struct http_cache_request *request = p_malloc(pool,
                                                  sizeof(*request));
    request->pool = pool;
    request->cache = cache;
    request->url = url;
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->item = NULL;
    request->info = info;

    cache_log(4, "http_cache: miss %s\n", url);

    url_stream_new(pool, cache->stock,
                   method, url,
                   headers == NULL ? NULL : headers_dup(pool, headers), body,
                   &http_cache_response_handler, request,
                   async_ref);
}

static void
http_cache_serve(struct http_cache_item *item,
                 pool_t pool,
                 const char *url __attr_unused, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    istream_t response_body;

    if (body != NULL)
        istream_close(body);

    cache_log(4, "http_cache: serve %s\n", url);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    /* XXX hold reference on item */
    response_body = istream_memory_new(pool, item->data, item->length);
    http_response_handler_invoke_response(&handler_ref, item->status,
                                          item->headers, response_body);
}

static void
http_cache_test(struct http_cache *cache, struct http_cache_item *item,
                pool_t pool,
                http_method_t method, const char *url,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    struct http_cache_request *request = p_malloc(pool,
                                                  sizeof(*request));
    request->pool = pool;
    request->cache = cache;
    request->url = url;
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->item = item;
    request->info = &item->info;

    cache_log(4, "http_cache: test %s\n", url);

    if (headers == NULL)
        headers = strmap_new(pool, 16);

    if (item->info.last_modified != NULL)
        strmap_put(headers, "if-modified-since", item->info.last_modified, 1);

    if (item->info.etag != NULL)
        strmap_put(headers, "if-none-match", item->info.etag, 1);

    url_stream_new(pool, cache->stock,
                   method, url,
                   headers_dup(pool, headers), body,
                   &http_cache_response_handler, request,
                   async_ref);
}

static void
http_cache_found(struct http_cache *cache, struct http_cache_item *item,
                 pool_t pool,
                 http_method_t method, const char *url,
                 struct strmap *headers, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (item->info.expires != (time_t)-1 && item->info.expires >= time(NULL))
        http_cache_serve(item, pool, url, body, handler, handler_ctx);
    else
        http_cache_test(cache, item, pool,
                        method, url, headers, body,
                        handler, handler_ctx, async_ref);
}

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method, const char *url,
                   struct strmap *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct http_cache_info *info;

    info = http_cache_request_evaluate(pool, method, headers, body);
    if (info != NULL) {
        struct http_cache_item *item
            = (struct http_cache_item *)cache_get(cache->cache, url);

        if (item == NULL)
            http_cache_miss(cache, info, pool,
                            method, url, headers, body,
                            handler, handler_ctx, async_ref);
        else
            http_cache_found(cache, item, pool,
                             method, url, headers, body,
                             handler, handler_ctx, async_ref);
    } else {
        cache_log(4, "http_cache: ignore %s\n", url);

        url_stream_new(pool, cache->stock,
                       method, url,
                       headers_dup(pool, headers), body,
                       handler, handler_ctx,
                       async_ref);
    }
}
