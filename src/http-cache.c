/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache.h"
#include "cache.h"
#include "url-stream.h"
#include "strmap.h"
#include "http-response.h"
#include "date.h"

#include <string.h>
#include <time.h>

struct http_cache {
    pool_t pool;
    struct cache *cache;
    struct hstock *stock;
};

struct http_cache_item {
    struct cache_item item;

    pool_t pool;

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

    time_t expires;

    http_status_t status;
    strmap_t headers;
    istream_t input;
    size_t length;
    struct growing_buffer *output;
};


static void
http_cache_put(struct http_cache_request *request)
{
    pool_t pool;
    struct http_cache_item *item;

    pool = pool_new_linear(request->cache->pool, "http_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));
    item->item.expires = request->expires;
    item->pool = pool;
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

/** check whether the HTTP response should be put into the cache */
static int
http_cache_evaluate(http_status_t status, strmap_t headers,
                    off_t body_available,
                    time_t *expires_r)
{
    time_t date, expires;
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

    p = strmap_get(headers, "expires");
    if (p != NULL) {
        expires = http_date_parse(p);
        if (expires == (time_t)-1 || expires < date)
            return 0;

        *expires_r = time(NULL) + (expires - date);
    } else
        return 0;

    return 1;
}


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

    available = body == NULL ? 0 : istream_available(body, 1);

    if (!http_cache_evaluate(status, headers, available, &request->expires)) {
        /* don't cache response */
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

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method, const char *url,
                   struct growing_buffer *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    if (method == HTTP_METHOD_GET && body == NULL) {
        struct http_cache_item *item
            = (struct http_cache_item *)cache_get(cache->cache, url);

        if (item == NULL) {
            struct http_cache_request *request = p_malloc(pool,
                                                          sizeof(*request));
            request->pool = pool;
            request->cache = cache;
            request->url = url;
            http_response_handler_set(&request->handler, handler, handler_ctx);

            url_stream_new(pool, cache->stock,
                           method, url,
                           headers, body,
                           &http_cache_response_handler, request,
                           async_ref);
        } else {
            /* XXX request with If-Modified-Since */
            struct http_response_handler_ref handler_ref;
            istream_t response_body;

            http_response_handler_set(&handler_ref, handler, handler_ctx);

            /* XXX hold reference on item */
            response_body = istream_memory_new(pool, item->data, item->length);
            http_response_handler_invoke_response(&handler_ref, item->status,
                                                  item->headers, response_body);
        }
    } else
        url_stream_new(pool, cache->stock,
                       method, url,
                       headers, body,
                       handler, handler_ctx,
                       async_ref);
}
