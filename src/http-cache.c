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
#include "background.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>

struct http_cache {
    pool_t pool;
    struct cache *cache;
    struct memcached_stock *memcached_stock;
    struct hstock *tcp_stock;

    struct list_head requests;

    struct background_manager background;
};

struct http_cache_flush {
    struct background_job background;
};

struct http_cache_request {
    struct list_head siblings;

    pool_t pool, caller_pool;

    /**
     * The cache object which got this request.
     */
    struct http_cache *cache;
    http_method_t method;
    struct uri_with_address *uwa;
    const char *url;

    /** headers from the original request */
    struct strmap *headers;

    struct http_response_handler_ref handler;

    /**
     * Information on the request passed to http_cache_request().
     */
    struct http_cache_info *info;

    /**
     * The document which was found in the cache, in case this is a
     * request to test the validity of the cache entry.  If this is
     * NULL, then we had a cache miss.
     */
    struct http_cache_document *document;

    /**
     * The response body from the http_cache_document.  This is not
     * used for the heap backend: it creates the #istream_t on demand
     * with http_cache_heap_istream().
     */
    istream_t document_body;

    /**
     * This struct holds response information while this module
     * receives the response body.
     */
    struct {
        http_status_t status;
        struct strmap *headers;

        /**
         * The response body istream we got from the http_request()
         * callback.
         */
        istream_t input;

        /**
         * The current size of #output.  We could use
         * growing_buffer_length() here, but that would be too
         * expensive.
         */
        size_t length;

        /**
         * A sink for the response body, read from #input.
         */
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
http_cache_memcached_put_callback(void *ctx)
{
    struct http_cache_request *request = ctx;

    list_remove(&request->siblings);
}

static void
http_cache_put(struct http_cache_request *request)
{
    assert(request != NULL);
    assert(request->info != NULL);

    cache_log(4, "http_cache: put %s\n", request->url);

    if (request->cache->cache != NULL)
        http_cache_heap_put(request->cache->cache, request->cache->pool, request->url,
                            request->info, request->headers, request->response.status,
                            request->response.headers, request->response.output);
    else {
        list_add(&request->siblings, &request->cache->requests);

        http_cache_memcached_put(request->pool, request->cache->memcached_stock,
                                 request->cache->pool,
                                 &request->cache->background,
                                 request->url,
                                 request->info,
                                 request->headers,
                                 request->response.status, request->response.headers,
                                 growing_buffer_istream(request->response.output),
                                 http_cache_memcached_put_callback, request,
                                 &request->async_ref);
    }
}

static void
http_cache_remove(struct http_cache *cache, const char *url,
                  struct http_cache_document *document)
{
    if (cache->cache != NULL)
        http_cache_heap_remove(cache->cache, url, document);
}

static void
http_cache_remove_url(struct http_cache *cache, const char *url)
{
    if (cache->cache != NULL)
        http_cache_heap_remove_url(cache->cache, url);
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
http_cache_serve(struct http_cache_request *request);


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

    list_remove(&request->siblings);

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(request);

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
        http_cache_serve(request);
        pool_unref(request->caller_pool);
        return;
    }

    if (request->document != NULL &&
        http_cache_prefer_cached(request->document, headers)) {
        cache_log(4, "http_cache: matching etag '%s' for %s, using cache entry\n",
                  request->document->info.etag, request->url);

        if (body != NULL)
            istream_close(body);

        http_cache_serve(request);
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

    if (request->document != NULL && request->cache->cache != NULL)
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

    if (request->document != NULL && request->cache->cache != NULL)
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
               struct memcached_stock *memcached_stock,
               struct hstock *tcp_stock)
{
    struct http_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->cache = memcached_stock == NULL && max_size > 0
        ? http_cache_heap_new(pool, max_size)
        : NULL;
    cache->memcached_stock = memcached_stock;
    cache->tcp_stock = tcp_stock;

    list_init(&cache->requests);
    background_manager_init(&cache->background);

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
    assert(request->response.input != NULL || request->cache->memcached_stock != NULL);
    assert(request->response.output != NULL);

    if (request->response.input != NULL)
        istream_close(request->response.input);
    else
        async_abort(&request->async_ref);
}

void
http_cache_close(struct http_cache *cache)
{

    while (!list_empty(&cache->requests)) {
        struct http_cache_request *request =
            list_head_to_request(cache->requests.next);

        http_cache_request_close(request);
    }

    background_manager_abort_all(&cache->background);

    if (cache->cache != NULL)
        http_cache_heap_free(cache->cache);
}

static void
http_cache_flush_callback(bool success, void *ctx)
{
    struct http_cache_flush *flush = ctx;

    background_manager_remove(&flush->background);

    if (success)
        cache_log(5, "http_cache_memcached: flushed\n");
    else
        cache_log(5, "http_cache_memcached: flush has failed\n");
}

void
http_cache_flush(struct http_cache *cache)
{
    if (cache->cache != NULL)
        http_cache_heap_flush(cache->cache);
    else {
        pool_t pool = pool_new_linear(cache->pool,
                                      "http_cache_memcached_flush", 1024);
        struct http_cache_flush *flush = p_malloc(pool, sizeof(*flush));

        http_cache_memcached_flush(pool, cache->memcached_stock,
                                   http_cache_flush_callback, flush,
                                   background_job_add(&cache->background,
                                                      &flush->background));
        pool_unref(pool);
    }
}

static void
http_cache_miss(struct http_cache *cache, pool_t caller_pool,
                struct http_cache_info *info,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers,
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
                 headers == NULL ? NULL : headers_dup(pool, headers), NULL,
                 &http_cache_response_handler, request,
                 &request->async_ref);
    pool_unref(pool);
}

static void
http_cache_heap_serve(struct cache *cache,
                      struct http_cache_document *document,
                      pool_t pool,
                      const char *url __attr_unused,
                      const struct http_response_handler *handler,
                      void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    istream_t response_body;

    cache_log(4, "http_cache: serve %s\n", url);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    response_body = http_cache_heap_istream(pool, cache, document);

    http_response_handler_invoke_response(&handler_ref, document->status,
                                          document->headers, response_body);
}

static void
http_cache_memcached_serve(struct http_cache_request *request)
{
    cache_log(4, "http_cache: serve %s\n", request->url);

    http_response_handler_invoke_response(&request->handler,
                                          request->document->status,
                                          request->document->headers,
                                          request->document_body);
}

static void
http_cache_serve(struct http_cache_request *request)
{
    if (request->cache->cache != NULL)
        http_cache_heap_serve(request->cache->cache, request->document,
                              request->pool, request->url,
                              request->handler.handler, request->handler.ctx);
    else
        http_cache_memcached_serve(request);
}

static void
http_cache_test(struct http_cache_request *request,
                http_method_t method,
                struct uri_with_address *uwa,
                struct strmap *headers)
{
    struct http_cache *cache = request->cache;
    struct http_cache_document *document = request->document;

    cache_log(4, "http_cache: test %s\n", uwa->uri);

    if (headers == NULL)
        headers = strmap_new(request->pool, 16);

    if (document->info.last_modified != NULL)
        strmap_set(headers, "if-modified-since",
                   document->info.last_modified);

    if (document->info.etag != NULL)
        strmap_set(headers, "if-none-match", document->info.etag);

    http_request(request->pool, cache->tcp_stock,
                 method, uwa,
                 headers_dup(request->pool, headers), NULL,
                 &http_cache_response_handler, request,
                 &request->async_ref);
}

static void
http_cache_heap_test(struct http_cache *cache, pool_t caller_pool,
                     struct http_cache_info *info,
                     struct http_cache_document *document,
                     http_method_t method,
                     struct uri_with_address *uwa,
                     struct strmap *headers,
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

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_cache_test(request, method, uwa, headers);
    pool_unref(request->pool);
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
                 struct strmap *headers,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (http_cache_may_serve(info, document))
        http_cache_heap_serve(cache->cache, document, pool,
                              uwa->uri, handler, handler_ctx);
    else
        http_cache_heap_test(cache, pool, info, document,
                             method, uwa, headers,
                             handler, handler_ctx, async_ref);
}

static void
http_cache_heap_use(struct http_cache *cache,
                    pool_t pool,
                    http_method_t method,
                    struct uri_with_address *uwa,
                    struct strmap *headers,
                    struct http_cache_info *info,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct http_cache_document *document
        = http_cache_heap_get(cache->cache, uwa->uri, headers);

    if (document == NULL)
        http_cache_miss(cache, pool, info,
                        method, uwa, headers,
                        handler, handler_ctx, async_ref);
    else
        http_cache_found(cache, info, document, pool,
                         method, uwa, headers,
                         handler, handler_ctx, async_ref);
}

static void
http_cache_memcached_forward(struct http_cache_request *request,
                             const struct http_response_handler *handler,
                             void *handler_ctx)
{
    struct growing_buffer *headers2;

    cache_log(4, "http_cache: miss %s\n", request->url);

    headers2 = request->headers == NULL
        ? NULL : headers_dup(request->pool, request->headers);

    http_request(request->pool, request->cache->tcp_stock,
                 request->method, request->uwa,
                 headers2, NULL,
                 handler, handler_ctx, &request->async_ref);
}

static void
http_cache_memcached_miss(struct http_cache_request *request)
{
    struct http_cache_info *info = request->info;

    if (info->only_if_cached) {
        http_response_handler_invoke_response(&request->handler,
                                              HTTP_STATUS_GATEWAY_TIMEOUT,
                                              NULL, NULL);
        return;
    }

    cache_log(4, "http_cache: miss %s\n", request->url);

    request->document = NULL;

    http_cache_memcached_forward(request,
                                 &http_cache_response_handler, request);
}

static void
http_cache_memcached_get_callback(struct http_cache_document *document,
                                  istream_t body, void *ctx)
{
    struct http_cache_request *request = ctx;

    if (document == NULL) {
        http_cache_memcached_miss(request);
        return;
    }

    if (http_cache_may_serve(request->info, document)) {
        cache_log(4, "http_cache: serve %s\n", request->url);

        http_response_handler_invoke_response(&request->handler,
                                              document->status,
                                              document->headers,
                                              body);
        pool_unref(request->caller_pool);
    } else {
        request->document = document;
        request->document_body = istream_hold_new(request->pool, body);

        http_cache_test(request, request->method, request->uwa,
                        request->headers);
    }
}

static void
http_cache_memcached_use(struct http_cache *cache,
                         pool_t caller_pool,
                         http_method_t method,
                         struct uri_with_address *uwa,
                         struct strmap *headers,
                         struct http_cache_info *info,
                         const struct http_response_handler *handler,
                         void *handler_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_cache_request *request;
    pool_t pool;

    assert(cache->memcached_stock != NULL);

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "http_cache_request", 8192);

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->cache = cache;
    request->method = method;
    request->uwa = uwa;
    request->url = uwa->uri;
    request->headers = headers;
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->info = info;

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref(caller_pool);
    http_cache_memcached_get(pool, cache->memcached_stock,
                             request->cache->pool,
                             &cache->background,
                             uwa->uri, headers,
                             http_cache_memcached_get_callback, request,
                             &request->async_ref);
    pool_unref(pool);
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

    info = cache->cache != NULL || cache->memcached_stock != NULL
        ? http_cache_request_evaluate(pool, method, uwa->uri, headers, body)
        : NULL;
    if (info != NULL) {
        assert(body == NULL);

        if (cache->cache != NULL)
            http_cache_heap_use(cache, pool, method, uwa, headers, info,
                                handler, handler_ctx, async_ref);
        else
            http_cache_memcached_use(cache, pool, method, uwa, headers,
                                     info, handler, handler_ctx, async_ref);
    } else {
        struct growing_buffer *headers2;

        if (http_cache_request_invalidate(method))
            http_cache_remove_url(cache, uwa->uri);

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
