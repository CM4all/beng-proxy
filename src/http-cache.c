/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
#include "cache.h"
#include "http-request.h"
#include "header-writer.h"
#include "strmap.h"
#include "http-response.h"
#include "uri-address.h"
#include "strref2.h"
#include "growing-buffer.h"
#include "tpool.h"
#include "http-util.h"
#include "async.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

struct http_cache {
    pool_t pool;
    struct cache *cache;
    struct hstock *tcp_stock;

    struct list_head requests;
};

struct http_cache_item {
    struct cache_item item;

    pool_t pool;

    struct http_cache_document document;
};

struct http_cache_request {
    struct list_head siblings;

    pool_t pool, caller_pool;
    struct http_cache *cache;
    const char *url;
    struct strmap *headers;
    struct http_response_handler_ref handler;

    struct http_cache_item *item;
    struct http_cache_info *info;

    struct {
        http_status_t status;
        struct strmap *headers;
        istream_t input;
        size_t length;
        struct growing_buffer *output;
    } response;

    struct async_operation operation;
    struct async_operation_ref async_ref;
};

static struct http_cache_request *
http_cache_request_dup(pool_t pool, const struct http_cache_request *src)
{
    struct http_cache_request *dest = p_malloc(pool, sizeof(*dest));

    dest->pool = pool;
    dest->caller_pool = src->caller_pool;
    dest->cache = src->cache;
    dest->url = p_strdup(pool, src->url);
    dest->headers = src->headers == NULL
        ? NULL : strmap_dup(pool, src->headers);
    dest->handler = src->handler;
    dest->info = http_cache_info_dup(pool, src->info);
    return dest;
}

static bool
http_cache_item_match(const struct cache_item *_item, void *ctx)
{
    const struct http_cache_item *item =
        (const struct http_cache_item *)_item;
    struct strmap *headers = ctx;

    return http_cache_document_fits(&item->document, headers);
}

static void
http_cache_put(struct http_cache_request *request)
{
    pool_t pool;
    struct http_cache_item *item;
    time_t expires;

    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "http_cache: put %s\n", request->url);

    pool = pool_new_linear(request->cache->pool, "http_cache_item", 1024);
    item = p_malloc(pool, sizeof(*item));

    if (request->info->expires == (time_t)-1)
        /* there is no Expires response header; keep it in the cache
           for 1 hour, but check with If-Modified-Since */
        expires = time(NULL) + 3600;
    else
        expires = request->info->expires;

    cache_item_init(&item->item, expires, request->response.length);

    item->pool = pool;

    http_cache_document_init(&item->document, pool, request->info,
                             request->headers,
                             request->response.status,
                             request->response.headers,
                             request->response.output);
    assert(item->document.size == request->response.length);

    cache_put_match(request->cache->cache, p_strdup(pool, request->url),
                    &item->item,
                    http_cache_item_match, request->headers);
}

static void
http_cache_remove(struct http_cache *cache, const char *url,
                  struct http_cache_item *item)
{
    cache_remove_item(cache->cache, url, &item->item);
    cache_item_unlock(cache->cache, &item->item);
}

static void
http_cache_serve(struct http_cache *cache, struct http_cache_item *item,
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

    request->response.length += length;
    if (request->response.length > (size_t)cacheable_size_limit) {
        istream_close(request->response.input);
        return 0;
    }

    growing_buffer_write_buffer(request->response.output, data, length);
    return length;
}

static void
http_cache_response_body_eof(void *ctx)
{
    struct http_cache_request *request = ctx;

    request->response.input = NULL;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(request);

    list_remove(&request->siblings);
    pool_unref(request->pool);
}

static void
http_cache_response_body_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: body_abort %s\n", request->url);

    request->response.input = NULL;

    list_remove(&request->siblings);
    pool_unref(request->pool);
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
http_cache_response_response(http_status_t status, struct strmap *headers,
                             istream_t body,
                             void *ctx)
{
    struct http_cache_request *request = ctx;
    off_t available;
    pool_t caller_pool;

    if (request->item != NULL && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == NULL);

        cache_log(5, "http_cache: not_modified %s\n", request->url);
        http_cache_serve(request->cache, request->item, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->item != NULL &&
        http_cache_prefer_cached(&request->item->document, headers)) {
        cache_log(4, "http_cache: matching etag '%s' for %s, using cache entry\n",
                  request->item->document.info.etag, request->url);

        if (body != NULL)
            istream_close(body);

        http_cache_serve(request->cache, request->item, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->item != NULL)
        http_cache_remove(request->cache, request->url, request->item);

    available = body == NULL ? 0 : istream_available(body, true);

    if (!http_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", request->url);

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);
        pool_unref(request->caller_pool);
        return;
    }

    if (body == NULL) {
        request->response.output = NULL;
        http_cache_put(request);
    } else {
        pool_t pool;
        size_t buffer_size;

        /* move all this stuff to a new pool, so istream_tee's second
           head can continue to fill the cache even if our caller gave
           up on it */
        pool = pool_new_linear(request->cache->pool, "http_cache_tee", 1024);
        request = http_cache_request_dup(pool, request);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body, false);

        request->response.status = status;
        request->response.headers = strmap_dup(request->pool, headers);
        request->response.length = 0;

        istream_assign_handler(&request->response.input,
                               istream_tee_second(body),
                               &http_cache_response_body_handler, request,
                               0);

        if (available == (off_t)-1 || available < 256)
            buffer_size = 1024;
        else if (available > 16384)
            buffer_size = 16384;
        else
            buffer_size = (size_t)available;
        request->response.output = growing_buffer_new(request->pool,
                                                      buffer_size);

        list_add(&request->siblings, &request->cache->requests);

        pool_ref(request->pool);
    }

    caller_pool = request->caller_pool;
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
http_cache_response_abort(void *ctx)
{
    struct http_cache_request *request = ctx;

    cache_log(4, "http_cache: response_abort %s\n", request->url);

    if (request->item != NULL)
        cache_item_unlock(request->cache->cache, &request->item->item);

    http_response_handler_invoke_abort(&request->handler);
    pool_unref(request->caller_pool);
}

static const struct http_response_handler http_cache_response_handler = {
    .response = http_cache_response_response,
    .abort = http_cache_response_abort,
};


/*
 * async operation
 *
 */

static struct http_cache_request *
async_to_request(struct async_operation *ao)
{
    return (struct http_cache_request*)(((char*)ao) - offsetof(struct http_cache_request, operation));
}

static void
http_cache_abort(struct async_operation *ao)
{
    struct http_cache_request *request = async_to_request(ao);
    pool_t caller_pool = request->caller_pool;

    if (request->item != NULL)
        cache_item_unlock(request->cache->cache, &request->item->item);

    async_abort(&request->async_ref);

    /* the async_abort() call may have destroyed request->pool, so
       we're using a local variable instead of dereferencing
       request->caller_pool */
    pool_unref(caller_pool);
}

static const struct async_operation_class http_cache_async_operation = {
    .abort = http_cache_abort,
};


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
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(pool_t pool, size_t max_size,
               struct hstock *tcp_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = cache_new(pool, &http_cache_class,
                             65521, max_size);
    cache->tcp_stock = tcp_stock;

    list_init(&cache->requests);

    return cache;
}

static inline struct http_cache_request *
list_head_to_request(struct list_head *head)
{
    return (struct http_cache_request *)(((char*)head) - offsetof(struct http_cache_request, siblings));
}

static void
http_cache_request_close(struct http_cache_request *request)
{
    assert(request != NULL);
    assert(request->response.input != NULL);
    assert(request->response.output != NULL);

    istream_close(request->response.input);
}

void
http_cache_close(struct http_cache *cache)
{

    while (!list_empty(&cache->requests)) {
        struct http_cache_request *request =
            list_head_to_request(cache->requests.next);

        http_cache_request_close(request);
    }

    cache_close(cache->cache);
}

void
http_cache_flush(struct http_cache *cache)
{
    cache_flush(cache->cache);
}

static void
http_cache_miss(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    pool_t pool;
    struct http_cache_request *request;

    if (info->only_if_cached) {
        http_response_handler_direct_response(handler, handler_ctx,
                                              HTTP_STATUS_GATEWAY_TIMEOUT,
                                              NULL, NULL);
        return;
    }

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "http_cache_request", 8192);

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    request->url = uwa->uri;
    request->headers = headers == NULL ? NULL : strmap_dup(pool, headers);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->item = NULL;
    request->info = info;

    cache_log(4, "http_cache: miss %s\n", uwa->uri);

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_request(pool, cache->tcp_stock,
                 method, uwa,
                 headers == NULL ? NULL : headers_dup(pool, headers), body,
                 &http_cache_response_handler, request,
                 &request->async_ref);
    pool_unref(pool);
}

static istream_t
http_cache_document_istream(pool_t pool,
                            const struct http_cache_document *document)
{
    return document->size > 0
        ? istream_memory_new(pool, document->data, document->size)
        : istream_null_new(pool);
}

static void
http_cache_serve(struct http_cache *cache, struct http_cache_item *item,
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

    response_body = http_cache_document_istream(pool, &item->document);
    response_body = istream_unlock_new(pool, response_body,
                                       cache->cache, &item->item);

    http_response_handler_invoke_response(&handler_ref, item->document.status,
                                          item->document.headers, response_body);
}

static void
http_cache_test(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                struct http_cache_item *item,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers, istream_t body,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool_t pool = pool_new_linear(cache->pool, "http_cache_request", 8192);
    struct http_cache_request *request = p_malloc(pool,
                                                  sizeof(*request));
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    request->url = uwa->uri;
    request->headers = headers == NULL ? NULL : strmap_dup(pool, headers);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    cache_item_lock(&item->item);
    request->item = item;
    request->info = info;

    cache_log(4, "http_cache: test %s\n", uwa->uri);

    if (headers == NULL)
        headers = strmap_new(pool, 16);

    if (item->document.info.last_modified != NULL)
        strmap_set(headers, "if-modified-since",
                   item->document.info.last_modified);

    if (item->document.info.etag != NULL)
        strmap_set(headers, "if-none-match", item->document.info.etag);

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_request(pool, cache->tcp_stock,
                 method, uwa,
                 headers_dup(pool, headers), body,
                 &http_cache_response_handler, request,
                 &request->async_ref);
    pool_unref(pool);
}

static bool
http_cache_may_serve(struct http_cache_info *info,
                     const struct http_cache_document *document)
{
    return info->only_if_cached ||
        (document->info.expires != (time_t)-1 &&
         document->info.expires >= time(NULL));
}

static void
http_cache_found(struct http_cache *cache,
                 struct http_cache_info *info,
                 struct http_cache_item *item,
                 pool_t pool,
                 http_method_t method,
                 struct uri_with_address *uwa,
                 struct strmap *headers, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (http_cache_may_serve(info, &item->document))
        http_cache_serve(cache, item, pool,
                         uwa->uri, body, handler, handler_ctx);
    else
        http_cache_test(cache, pool, info, item,
                        method, uwa, headers, body,
                        handler, handler_ctx, async_ref);
}

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method,
                   struct uri_with_address *uwa,
                   struct strmap *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct http_cache_info *info;

    info = http_cache_request_evaluate(pool, method, uwa->uri, headers, body);
    if (info != NULL) {
        struct http_cache_item *item
            = (struct http_cache_item *)cache_get_match(cache->cache, uwa->uri,
                                                        http_cache_item_match,
                                                        headers);

        if (item == NULL)
            http_cache_miss(cache, pool, info,
                            method, uwa, headers, body,
                            handler, handler_ctx, async_ref);
        else
            http_cache_found(cache, info, item, pool,
                             method, uwa, headers, body,
                             handler, handler_ctx, async_ref);
    } else {
        struct growing_buffer *headers2;

        if (http_cache_request_invalidate(method))
            cache_remove(cache->cache, uwa->uri);

        cache_log(4, "http_cache: ignore %s\n", uwa->uri);

        headers2 = headers == NULL
            ? NULL : headers_dup(pool, headers);

        http_request(pool, cache->tcp_stock,
                     method, uwa,
                     headers2, body,
                     handler, handler_ctx,
                     async_ref);
    }
}
