/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_internal.hxx"
#include "http_cache_heap.hxx"
#include "http_cache_memcached.hxx"
#include "resource_loader.hxx"
#include "strmap.h"
#include "http_response.hxx"
#include "resource_address.hxx"
#include "strref2.h"
#include "http_util.hxx"
#include "async.h"
#include "background.h"
#include "istream.h"
#include "cache.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "istream_rubber.hxx"
#include "istream_tee.h"
#include "util/Cast.hxx"

#include <glib.h>

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

struct http_cache {
    struct pool *pool;

    Rubber *rubber;

    struct http_cache_heap heap;

    struct memcached_stock *memcached_stock;

    struct resource_loader *resource_loader;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    struct list_head requests;

    struct background_manager background;
};

struct http_cache_flush {
    struct background_job background;
};

struct http_cache_request {
    struct list_head siblings;

    struct pool *pool, *caller_pool;

#ifndef NDEBUG
    struct pool_notify_state caller_pool_notify;
#endif

    unsigned session_sticky;

    /**
     * The cache object which got this request.
     */
    struct http_cache *cache;
    http_method_t method;
    const struct resource_address *address;

    /**
     * The cache key used to address the associated cache document.
     */
    const char *key;

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
     * nullptr, then we had a cache miss.
     */
    struct http_cache_document *document;

    /**
     * The response body from the http_cache_document.  This is not
     * used for the heap backend: it creates the #istream on demand
     * with http_cache_heap_istream().
     */
    struct istream *document_body;

    /**
     * This struct holds response information while this module
     * receives the response body.
     */
    struct {
        http_status_t status;
        struct strmap *headers;
    } response;

    struct async_operation operation;
    struct async_operation_ref async_ref;

    static http_cache_request *FromSiblings(list_head *lh) {
        return ContainerCast(lh, http_cache_request, siblings);
    }

    static http_cache_request *FromAsync(async_operation *ao) {
        return ContainerCast(ao, http_cache_request, operation);
    }
};

static const char *
http_cache_key(struct pool *pool, const struct resource_address *address)
{
    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
    case RESOURCE_ADDRESS_LOCAL:
    case RESOURCE_ADDRESS_PIPE:
        /* not cacheable */
        return nullptr;

    case RESOURCE_ADDRESS_HTTP:
    case RESOURCE_ADDRESS_LHTTP:
    case RESOURCE_ADDRESS_AJP:
    case RESOURCE_ADDRESS_CGI:
    case RESOURCE_ADDRESS_FASTCGI:
    case RESOURCE_ADDRESS_WAS:
    case RESOURCE_ADDRESS_NFS:
        return resource_address_id(address, pool);
    }

    /* unreachable */
    assert(false);
    return nullptr;
}

static void
http_cache_memcached_put_callback(GError *error, void *ctx)
{
    background_job *job = (background_job *)ctx;

    if (error != nullptr) {
        cache_log(2, "http-cache: put failed: %s\n", error->message);
        g_error_free(error);
    }

    background_manager_remove(job);
}

static void
http_cache_put(struct http_cache_request *request,
               unsigned rubber_id, size_t size)
{
    assert(request != nullptr);
    assert(request->info != nullptr);

    cache_log(4, "http_cache: put %s\n", request->key);

    if (http_cache_heap_is_defined(&request->cache->heap))
        http_cache_heap_put(&request->cache->heap, request->key,
                            request->info, request->headers, request->response.status,
                            request->response.headers,
                            request->cache->rubber, rubber_id, size);
    else if (request->cache->memcached_stock != nullptr) {
        auto job = PoolAlloc<background_job>(request->pool);

        struct istream *value = rubber_id != 0
            ? istream_rubber_new(request->pool, request->cache->rubber,
                                 rubber_id, 0, size, true)
            : nullptr;

        http_cache_memcached_put(request->pool, request->cache->memcached_stock,
                                 request->cache->pool,
                                 &request->cache->background,
                                 request->key,
                                 request->info,
                                 request->headers,
                                 request->response.status, request->response.headers,
                                 value,
                                 http_cache_memcached_put_callback, job,
                                 background_job_add(&request->cache->background,
                                                    job));
    }
}

static void
http_cache_remove(struct http_cache *cache, const char *url,
                  struct http_cache_document *document)
{
    if (http_cache_heap_is_defined(&cache->heap))
        http_cache_heap_remove(&cache->heap, url, document);
}

static void
http_cache_remove_url(struct http_cache *cache, const char *url,
                      struct strmap *headers)
{
    if (http_cache_heap_is_defined(&cache->heap))
        http_cache_heap_remove_url(&cache->heap, url, headers);
    else if (cache->memcached_stock != nullptr)
        http_cache_memcached_remove_uri_match(cache->memcached_stock,
                                              cache->pool, &cache->background,
                                              url, headers);
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
    http_cache_heap_unlock(&cache->heap, document);
}

static void
http_cache_serve(struct http_cache_request *request);

/*
 * sink_rubber handler
 *
 */

static void
http_cache_rubber_done(unsigned rubber_id, size_t size, void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    async_ref_clear(&request->async_ref);
    list_remove(&request->siblings);

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(request, rubber_id, size);
}

static void
http_cache_rubber_oom(void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    cache_log(4, "http_cache: oom %s\n", request->key);

    async_ref_clear(&request->async_ref);
    list_remove(&request->siblings);
}

static void
http_cache_rubber_too_large(void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    cache_log(4, "http_cache: too large %s\n", request->key);

    async_ref_clear(&request->async_ref);
    list_remove(&request->siblings);
}

static void
http_cache_rubber_error(GError *error, void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    cache_log(4, "http_cache: body_abort %s: %s\n",
              request->key, error->message);
    g_error_free(error);

    async_ref_clear(&request->async_ref);
    list_remove(&request->siblings);
}

static const struct sink_rubber_handler http_cache_rubber_handler = {
    .done = http_cache_rubber_done,
    .out_of_memory = http_cache_rubber_oom,
    .too_large = http_cache_rubber_too_large,
    .error = http_cache_rubber_error,
};

/*
 * http response handler
 *
 */

static void
http_cache_response_response(http_status_t status, struct strmap *headers,
                             struct istream *body,
                             void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;
    struct http_cache *cache = request->cache;
    struct http_cache_document *locked_document =
        http_cache_heap_is_defined(&cache->heap) ? request->document : nullptr;
    off_t available;

    if (request->document != nullptr && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == nullptr);

        struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

        cache_log(5, "http_cache: not_modified %s\n", request->key);
        http_cache_serve(request);

        pool_unref_denotify(caller_pool, &notify);

        if (locked_document != nullptr)
            http_cache_unlock(cache, locked_document);

        return;
    }

    if (request->document != nullptr &&
        http_cache_prefer_cached(request->document, headers)) {
        cache_log(4, "http_cache: matching etag '%s' for %s, using cache entry\n",
                  request->document->info.etag, request->key);

        if (body != nullptr)
            istream_close_unused(body);

        struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

        http_cache_serve(request);

        pool_unref_denotify(caller_pool, &notify);

        if (locked_document != nullptr)
            http_cache_unlock(cache, locked_document);

        return;
    }

    async_operation_finished(&request->operation);

    if (request->document != nullptr)
        http_cache_remove(request->cache, request->key, request->document);

    if (request->document != nullptr &&
        !http_cache_heap_is_defined(&cache->heap) &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    available = body == nullptr ? 0 : istream_available(body, true);

    if (!http_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", request->key);

        struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

        http_response_handler_invoke_response(&request->handler, status,
                                              headers, body);

        pool_unref_denotify(caller_pool, &notify);
        return;
    }

    request->response.status = status;
    request->response.headers = headers != nullptr
        ? strmap_dup(request->pool, headers, 17)
        : nullptr;

    struct istream *const input = body;
    if (body == nullptr) {
        http_cache_put(request, 0, 0);
    } else {
        /* request->info was allocated from the caller pool; duplicate
           it to keep it alive even after the caller pool is
           destroyed */
        request->key = p_strdup(request->pool, request->key);
        request->info = http_cache_info_dup(request->pool, request->info);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body, false, false);

        list_add(&request->siblings, &request->cache->requests);

        /* we need this pool reference because the http-client will
           release our pool when our response handler closes the "tee"
           body stream within the callback */
        pool_ref(request->pool);

        sink_rubber_new(request->pool, istream_tee_second(body),
                        cache->rubber, cacheable_size_limit,
                        &http_cache_rubber_handler, request,
                        &request->async_ref);
    }

    struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

    http_response_handler_invoke_response(&request->handler, status,
                                          headers, body);

    pool_unref_denotify(caller_pool, &notify);

    if (input != nullptr) {
        if (async_ref_defined(&request->async_ref))
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_read(input);

        pool_unref(request->pool);
    }
}

static void
http_cache_response_abort(GError *error, void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    g_prefix_error(&error, "http_cache %s: ", request->key);

    if (request->document != nullptr &&
        http_cache_heap_is_defined(&request->cache->heap))
        http_cache_unlock(request->cache, request->document);

    if (request->document != nullptr &&
        !http_cache_heap_is_defined(&request->cache->heap) &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
    struct pool_notify_state notify;
    pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

    async_operation_finished(&request->operation);
    http_response_handler_invoke_abort(&request->handler, error);

    pool_unref_denotify(caller_pool, &notify);
}

static const struct http_response_handler http_cache_response_handler = {
    .response = http_cache_response_response,
    .abort = http_cache_response_abort,
};


/*
 * async operation
 *
 */

static void
http_cache_abort(struct async_operation *ao)
{
    auto request = http_cache_request::FromAsync(ao);

    if (request->document != nullptr &&
        http_cache_heap_is_defined(&request->cache->heap))
        http_cache_unlock(request->cache, request->document);

    if (request->document != nullptr &&
        !http_cache_heap_is_defined(&request->cache->heap) &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    pool_unref_denotify(request->caller_pool,
                        &request->caller_pool_notify);

    async_abort(&request->async_ref);
}

static const struct async_operation_class http_cache_async_operation = {
    .abort = http_cache_abort,
};


/*
 * constructor and public methods
 *
 */

struct http_cache *
http_cache_new(struct pool *pool, size_t max_size,
               struct memcached_stock *memcached_stock,
               struct resource_loader *resource_loader)
{
    pool = pool_new_libc(pool, "http_cache");

    http_cache *cache = PoolAlloc<http_cache>(pool);
    cache->pool = pool;

    if (memcached_stock != nullptr || max_size > 0) {
        static const size_t max_memcached_rubber = 64 * 1024 * 1024;
        size_t rubber_size = max_size;
        if (memcached_stock != nullptr && rubber_size > max_memcached_rubber)
            rubber_size = max_memcached_rubber;

        cache->rubber = rubber_new(rubber_size);
        if (cache->rubber == nullptr) {
            fprintf(stderr, "Failed to allocate HTTP cache: %s\n",
                    strerror(errno));
            exit(2);
        }
    }

    if (memcached_stock == nullptr && max_size > 0)
        /* leave 12.5% of the rubber allocator empty, to increase the
           chances that a hole can be found for a new allocation, to
           reduce the pressure that rubber_compress() creates */
        http_cache_heap_init(&cache->heap, pool, max_size * 7 / 8);
    else
        http_cache_heap_clear(&cache->heap);

    cache->memcached_stock = memcached_stock;
    cache->resource_loader = resource_loader;

    list_init(&cache->requests);
    background_manager_init(&cache->background);

    return cache;
}

static void
http_cache_request_close(struct http_cache_request *request)
{
    assert(request != nullptr);

    list_remove(&request->siblings);
    async_abort(&request->async_ref);
}

void
http_cache_close(struct http_cache *cache)
{

    while (!list_empty(&cache->requests)) {
        auto request = http_cache_request::FromSiblings(cache->requests.next);

        http_cache_request_close(request);
    }

    background_manager_abort_all(&cache->background);

    if (http_cache_heap_is_defined(&cache->heap))
        http_cache_heap_deinit(&cache->heap);

    rubber_free(cache->rubber);
    pool_unref(cache->pool);
}

void
http_cache_fork_cow(struct http_cache *cache, bool inherit)
{
    if (http_cache_heap_is_defined(&cache->heap) ||
        cache->memcached_stock != nullptr)
        rubber_fork_cow(cache->rubber, inherit);
}

void
http_cache_get_stats(const struct http_cache *cache, struct cache_stats *data)
{
    if (http_cache_heap_is_defined(&cache->heap))
        http_cache_heap_get_stats(&cache->heap, cache->rubber, data);
    else
        memset(data, 0, sizeof(*data));
}

static void
http_cache_flush_callback(bool success, GError *error, void *ctx)
{
    struct http_cache_flush *flush = (struct http_cache_flush *)ctx;

    background_manager_remove(&flush->background);

    if (success)
        cache_log(5, "http_cache_memcached: flushed\n");
    else if (error != nullptr) {
        cache_log(5, "http_cache_memcached: flush has failed: %s\n",
                  error->message);
        g_error_free(error);
    } else
        cache_log(5, "http_cache_memcached: flush has failed\n");
}

void
http_cache_flush(struct http_cache *cache)
{
    if (http_cache_heap_is_defined(&cache->heap))
        http_cache_heap_flush(&cache->heap);
    else if (cache->memcached_stock != nullptr) {
        struct pool *pool = pool_new_linear(cache->pool,
                                            "http_cache_memcached_flush", 1024);
        auto flush = PoolAlloc<struct http_cache_flush>(pool);

        http_cache_memcached_flush(pool, cache->memcached_stock,
                                   http_cache_flush_callback, flush,
                                   background_job_add(&cache->background,
                                                      &flush->background));
        pool_unref(pool);
    }

    if (cache->rubber != nullptr)
        rubber_compress(cache->rubber);
}

/**
 * A resource was not found in the cache.
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_miss(struct http_cache *cache, struct pool *caller_pool,
                unsigned session_sticky,
                struct http_cache_info *info,
                http_method_t method,
                const struct resource_address *address,
                struct strmap *headers,
                const struct http_response_handler *handler,
                void *handler_ctx,
                struct async_operation_ref *async_ref)
{
    struct pool *pool;

    if (info->only_if_cached) {
        http_response_handler_direct_response(handler, handler_ctx,
                                              HTTP_STATUS_GATEWAY_TIMEOUT,
                                              nullptr, nullptr);
        return;
    }

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "http_cache_request", 8192);

    auto request = PoolAlloc<struct http_cache_request>(pool);
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->session_sticky = session_sticky;
    request->cache = cache;
    request->key = http_cache_key(pool, address);
    request->headers = headers == nullptr ? nullptr : strmap_dup(pool, headers, 17);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->document = nullptr;
    request->info = info;

    cache_log(4, "http_cache: miss %s\n", request->key);

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref_notify(caller_pool, &request->caller_pool_notify);
    resource_loader_request(cache->resource_loader, pool, session_sticky,
                            method, address,
                            HTTP_STATUS_OK, headers, nullptr,
                            &http_cache_response_handler, request,
                            &request->async_ref);
    pool_unref(pool);
}

/**
 * Send the cached document to the caller (heap version).
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_heap_serve(struct http_cache_heap *cache,
                      struct http_cache_document *document,
                      struct pool *pool,
                      const char *key gcc_unused,
                      const struct http_response_handler *handler,
                      void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;
    struct istream *response_body;

    cache_log(4, "http_cache: serve %s\n", key);

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    response_body = http_cache_heap_istream(pool, cache, document);

    http_response_handler_invoke_response(&handler_ref, document->status,
                                          document->headers, response_body);
}

/**
 * Send the cached document to the caller (memcached version).
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_memcached_serve(struct http_cache_request *request)
{
    cache_log(4, "http_cache: serve %s\n", request->key);

    async_operation_finished(&request->operation);
    http_response_handler_invoke_response(&request->handler,
                                          request->document->status,
                                          request->document->headers,
                                          request->document_body);
}

/**
 * Send the cached document to the caller.
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_serve(struct http_cache_request *request)
{
    if (http_cache_heap_is_defined(&request->cache->heap))
        http_cache_heap_serve(&request->cache->heap, request->document,
                              request->pool, request->key,
                              request->handler.handler, request->handler.ctx);
    else if (request->cache->memcached_stock != nullptr)
        http_cache_memcached_serve(request);
}

/**
 * Revalidate a cache entry.
 *
 * Caller pool is freed asynchronously.
 */
static void
http_cache_test(struct http_cache_request *request,
                http_method_t method,
                const struct resource_address *address,
                struct strmap *headers)
{
    struct http_cache *cache = request->cache;
    struct http_cache_document *document = request->document;

    cache_log(4, "http_cache: test %s\n", request->key);

    if (headers == nullptr)
        headers = strmap_new(request->pool, 16);

    if (document->info.last_modified != nullptr)
        strmap_set(headers, "if-modified-since",
                   document->info.last_modified);

    if (document->info.etag != nullptr)
        strmap_set(headers, "if-none-match", document->info.etag);

    resource_loader_request(cache->resource_loader, request->pool,
                            request->session_sticky,
                            method, address,
                            HTTP_STATUS_OK, headers, nullptr,
                            &http_cache_response_handler, request,
                            &request->async_ref);
}

/**
 * Revalidate a cache entry (heap version).
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_heap_test(struct http_cache *cache, struct pool *caller_pool,
                     unsigned session_sticky,
                     struct http_cache_info *info,
                     struct http_cache_document *document,
                     http_method_t method,
                     const struct resource_address *address,
                     struct strmap *headers,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    struct pool *pool = pool_new_linear(cache->pool, "http_cache_request", 8192);
    auto request = PoolAlloc<struct http_cache_request>(pool);
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->session_sticky = session_sticky;
    request->cache = cache;
    request->key = http_cache_key(pool, address);
    request->headers = headers == nullptr ? nullptr : strmap_dup(pool, headers, 17);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    http_cache_lock(document);
    request->document = document;
    request->info = info;

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref_notify(caller_pool, &request->caller_pool_notify);
    http_cache_test(request, method, address, headers);
    pool_unref(pool);
}

static bool
http_cache_may_serve(struct http_cache_info *info,
                     const struct http_cache_document *document)
{
    return info->only_if_cached ||
        (document->info.expires != (time_t)-1 &&
         document->info.expires >= time(nullptr));
}

/**
 * The requested document was found in the cache.  It is either served
 * or revalidated.
 *
 * Caller pool is referenced synchronously and freed asynchronously
 * (as needed).
 */
static void
http_cache_found(struct http_cache *cache,
                 struct http_cache_info *info,
                 struct http_cache_document *document,
                 struct pool *pool,
                 unsigned session_sticky,
                 http_method_t method,
                 const struct resource_address *address,
                 struct strmap *headers,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    if (http_cache_may_serve(info, document))
        http_cache_heap_serve(&cache->heap, document, pool,
                              http_cache_key(pool, address),
                              handler, handler_ctx);
    else
        http_cache_heap_test(cache, pool, session_sticky, info, document,
                             method, address, headers,
                             handler, handler_ctx, async_ref);
}

/**
 * Query the heap cache.
 *
 * Caller pool is referenced synchronously and freed asynchronously
 * (as needed).
 */
static void
http_cache_heap_use(struct http_cache *cache,
                    struct pool *pool, unsigned session_sticky,
                    http_method_t method,
                    const struct resource_address *address,
                    struct strmap *headers,
                    struct http_cache_info *info,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct http_cache_document *document
        = http_cache_heap_get(&cache->heap, http_cache_key(pool, address),
                              headers);

    if (document == nullptr)
        http_cache_miss(cache, pool, session_sticky, info,
                        method, address, headers,
                        handler, handler_ctx, async_ref);
    else
        http_cache_found(cache, info, document, pool, session_sticky,
                         method, address, headers,
                         handler, handler_ctx, async_ref);
}

/**
 * Forward the HTTP request to the real server.
 *
 * Caller pool is freed asynchronously.
 */
static void
http_cache_memcached_forward(struct http_cache_request *request,
                             const struct http_response_handler *handler,
                             void *handler_ctx)
{
    resource_loader_request(request->cache->resource_loader, request->pool,
                            request->session_sticky,
                            request->method, request->address,
                            HTTP_STATUS_OK, request->headers, nullptr,
                            handler, handler_ctx, &request->async_ref);
}

/**
 * A resource was not found in the cache.
 *
 * Caller pool is freed (asynchronously).
 */
static void
http_cache_memcached_miss(struct http_cache_request *request)
{
    struct http_cache_info *info = request->info;

    if (info->only_if_cached) {
        struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

        async_operation_finished(&request->operation);
        http_response_handler_invoke_response(&request->handler,
                                              HTTP_STATUS_GATEWAY_TIMEOUT,
                                              nullptr, nullptr);

        pool_unref_denotify(caller_pool, &notify);
        return;
    }

    cache_log(4, "http_cache: miss %s\n", request->key);

    request->document = nullptr;

    http_cache_memcached_forward(request,
                                 &http_cache_response_handler, request);
}

/**
 * The memcached-client callback.
 *
 * Caller pool is freed (asynchronously).
 */
static void
http_cache_memcached_get_callback(struct http_cache_document *document,
                                  struct istream *body, GError *error, void *ctx)
{
    struct http_cache_request *request = (struct http_cache_request *)ctx;

    if (document == nullptr) {
        if (error != nullptr) {
            cache_log(2, "http_cache: get failed: %s\n", error->message);
            g_error_free(error);
        }

        http_cache_memcached_miss(request);
        return;
    }

    if (http_cache_may_serve(request->info, document)) {
        cache_log(4, "http_cache: serve %s\n", request->key);

        struct pool *caller_pool = request->caller_pool;
#ifndef NDEBUG
        struct pool_notify_state notify;
        pool_notify_move(caller_pool, &request->caller_pool_notify, &notify);
#endif

        async_operation_finished(&request->operation);
        http_response_handler_invoke_response(&request->handler,
                                              document->status,
                                              document->headers,
                                              body);

        pool_unref_denotify(caller_pool, &notify);
    } else {
        request->document = document;
        request->document_body = istream_hold_new(request->pool, body);

        http_cache_test(request, request->method, request->address,
                        request->headers);
    }
}

/**
 * Query the resource from the memached server.
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_memcached_use(struct http_cache *cache,
                         struct pool *caller_pool, unsigned session_sticky,
                         http_method_t method,
                         const struct resource_address *address,
                         struct strmap *headers,
                         struct http_cache_info *info,
                         const struct http_response_handler *handler,
                         void *handler_ctx,
                         struct async_operation_ref *async_ref)
{
    struct pool *pool;

    assert(cache->memcached_stock != nullptr);

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(cache->pool, "http_cache_request", 8192);

    auto request = PoolAlloc<struct http_cache_request>(pool);
    request->pool = pool;
    request->caller_pool = caller_pool;
    request->session_sticky = session_sticky;
    request->cache = cache;
    request->method = method;
    request->address = resource_address_dup(pool, address);
    request->key = http_cache_key(pool, request->address);
    request->headers = headers == nullptr ? nullptr : strmap_dup(pool, headers, 17);
    http_response_handler_set(&request->handler, handler, handler_ctx);

    request->info = info;
    request->document = nullptr;

    async_init(&request->operation, &http_cache_async_operation);
    async_ref_set(async_ref, &request->operation);

    pool_ref_notify(caller_pool, &request->caller_pool_notify);
    http_cache_memcached_get(pool, cache->memcached_stock,
                             request->cache->pool,
                             &cache->background,
                             request->key, request->headers,
                             http_cache_memcached_get_callback, request,
                             &request->async_ref);
    pool_unref(pool);
}

void
http_cache_request(struct http_cache *cache,
                   struct pool *pool, unsigned session_sticky,
                   http_method_t method,
                   const struct resource_address *address,
                   struct strmap *headers, struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    const char *key = http_cache_heap_is_defined(&cache->heap) ||
        cache->memcached_stock != nullptr
        ? http_cache_key(pool, address)
        : nullptr;
    if (/* this address type cannot be cached; skip the rest of this
           library */
        key == nullptr ||
        /* don't cache a huge request URI; probably it contains lots
           and lots of unique parameters, and that's not worth the
           cache space anyway */
        strlen(key) > 8192) {
        resource_loader_request(cache->resource_loader, pool, session_sticky,
                                method, address,
                                HTTP_STATUS_OK, headers, body,
                                handler, handler_ctx,
                                async_ref);
        return;
    }

    struct http_cache_info *info =
        http_cache_request_evaluate(pool, method, address, headers, body);
    if (info != nullptr) {
        assert(body == nullptr);

        if (http_cache_heap_is_defined(&cache->heap))
            http_cache_heap_use(cache, pool, session_sticky,
                                method, address, headers, info,
                                handler, handler_ctx, async_ref);
        else if (cache->memcached_stock != nullptr)
            http_cache_memcached_use(cache, pool, session_sticky,
                                     method, address, headers,
                                     info, handler, handler_ctx, async_ref);
    } else {
        if (http_cache_request_invalidate(method))
            http_cache_remove_url(cache, key, headers);

        cache_log(4, "http_cache: ignore %s\n", key);

        resource_loader_request(cache->resource_loader, pool, session_sticky,
                                method, address,
                                HTTP_STATUS_OK, headers, body,
                                handler, handler_ctx,
                                async_ref);
    }
}
