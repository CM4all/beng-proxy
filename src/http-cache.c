/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-cache-internal.h"
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

struct http_cache_request {
    struct list_head siblings;

    pool_t pool, caller_pool;
    struct http_cache *cache;
    const char *url;
    struct strmap *headers;
    struct http_response_handler_ref handler;

    struct http_cache_document *document;
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

static void
http_cache_put(struct http_cache_request *request)
{
    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "http_cache: put %s\n", request->url);

    http_cache_heap_put(request->cache->cache, request->cache->pool, request->url,
                        request->info, request->headers, request->response.status,
                        request->response.headers, request->response.output);
}

static void
http_cache_remove(struct http_cache *cache, const char *url,
                  struct http_cache_document *document)
{
    http_cache_heap_remove(cache->cache, url, document);
}

static void
http_cache_lock(struct http_cache_document *document)
{
    http_cache_heap_lock(document);
}

static void
http_cache_unlock(struct http_cache *cache,
                  struct http_cache_document *document)
{
    http_cache_heap_unlock(cache->cache, document);
}

static void
http_cache_serve(struct http_cache *cache,
                 struct http_cache_document *document,
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

    if (request->document != NULL && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == NULL);

        cache_log(5, "http_cache: not_modified %s\n", request->url);
        http_cache_serve(request->cache, request->document, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->document != NULL &&
        http_cache_prefer_cached(request->document, headers)) {
        cache_log(4, "http_cache: matching etag '%s' for %s, using cache entry\n",
                  request->document->info.etag, request->url);

        if (body != NULL)
            istream_close(body);

        http_cache_serve(request->cache, request->document, request->pool,
                         request->url, NULL,
                         request->handler.handler, request->handler.ctx);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->document != NULL)
        http_cache_remove(request->cache, request->url, request->document);

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

    if (request->document != NULL)
        http_cache_unlock(request->cache, request->document);

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

    if (request->document != NULL)
        http_cache_unlock(request->cache, request->document);

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
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(pool_t pool, size_t max_size,
               struct hstock *tcp_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = max_size > 0
        ? http_cache_heap_new(pool, max_size)
        : NULL;
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

    if (cache->cache != NULL)
        http_cache_heap_free(cache->cache);
}

void
http_cache_flush(struct http_cache *cache)
{
    if (cache->cache != NULL)
        http_cache_heap_flush(cache->cache);
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

    request->document = NULL;
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
http_cache_serve(struct http_cache *cache,
                 struct http_cache_document *document,
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

    response_body = http_cache_document_istream(pool, document);
    response_body = http_cache_heap_wrap(pool, response_body,
                                         cache->cache, document);

    http_response_handler_invoke_response(&handler_ref, document->status,
                                          document->headers, response_body);
}

static void
http_cache_test(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                struct http_cache_document *document,
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

    http_cache_lock(document);
    request->document = document;
    request->info = info;

    cache_log(4, "http_cache: test %s\n", uwa->uri);

    if (headers == NULL)
        headers = strmap_new(pool, 16);

    if (document->info.last_modified != NULL)
        strmap_set(headers, "if-modified-since",
                   document->info.last_modified);

    if (document->info.etag != NULL)
        strmap_set(headers, "if-none-match", document->info.etag);

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
                 struct http_cache_document *document,
                 pool_t pool,
                 http_method_t method,
                 struct uri_with_address *uwa,
                 struct strmap *headers, istream_t body,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (http_cache_may_serve(info, document))
        http_cache_serve(cache, document, pool,
                         uwa->uri, body, handler, handler_ctx);
    else
        http_cache_test(cache, pool, info, document,
                        method, uwa, headers, body,
                        handler, handler_ctx, async_ref);
}

static void
http_cache_heap_use(struct http_cache *cache,
                    pool_t pool,
                    http_method_t method,
                    struct uri_with_address *uwa,
                    struct strmap *headers, istream_t body,
                    struct http_cache_info *info,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct http_cache_document *document
        = http_cache_heap_get(cache->cache, uwa->uri, headers);

    if (document == NULL)
        http_cache_miss(cache, pool, info,
                        method, uwa, headers, body,
                        handler, handler_ctx, async_ref);
    else
        http_cache_found(cache, info, document, pool,
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

    info = cache->cache != NULL
        ? http_cache_request_evaluate(pool, method, uwa->uri, headers, body)
        : NULL;
    if (info != NULL) {
        http_cache_heap_use(cache, pool, method, uwa, headers, body, info,
                            handler, handler_ctx, async_ref);
    } else {
        struct growing_buffer *headers2;

        if (http_cache_request_invalidate(method))
            http_cache_heap_remove_url(cache->cache, uwa->uri);

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
