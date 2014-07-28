/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_cache_internal.hxx"
#include "http_cache_document.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_heap.hxx"
#include "http_cache_memcached.hxx"
#include "resource_loader.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "resource_address.hxx"
#include "strref2.h"
#include "http_util.hxx"
#include "async.hxx"
#include "background.hxx"
#include "istream.h"
#include "cache.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "istream_rubber.hxx"
#include "istream_tee.h"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

#include <boost/intrusive/list.hpp>

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

class HttpCacheRequest {
public:
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

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

    HttpCacheRequest(struct pool &_pool, struct pool &_caller_pool,
                     unsigned _session_sticky,
                     struct http_cache &_cache,
                     http_method_t _method,
                     const struct resource_address &_address,
                     const char *_key,
                     struct strmap *_headers,
                     const struct http_response_handler &_handler,
                     void *_handler_ctx,
                     struct http_cache_info &_info,
                     struct async_operation_ref &_async_ref);

    HttpCacheRequest(const HttpCacheRequest &) = delete;

    static HttpCacheRequest *FromAsync(async_operation *ao) {
        return &ContainerCast2(*ao, &HttpCacheRequest::operation);
    }

    /**
     * Storing the response body in the rubber allocator has finished
     * (but may have failed).
     */
    void RubberStoreFinished();

    /**
     * Abort storing the response body in the rubber allocator.
     */
    void AbortRubberStore();
};

struct http_cache {
    struct pool &pool;

    Rubber *rubber;

    struct http_cache_heap heap;

    struct memcached_stock *memcached_stock;

    struct resource_loader &resource_loader;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    boost::intrusive::list<HttpCacheRequest,
                           boost::intrusive::member_hook<HttpCacheRequest,
                                                         HttpCacheRequest::SiblingsHook,
                                                         &HttpCacheRequest::siblings>,
                           boost::intrusive::constant_time_size<false>> requests;

    BackgroundManager background;

    http_cache(struct pool &_pool, size_t max_size,
               struct memcached_stock *_memcached_stock,
               struct resource_loader &_resource_loader);

    http_cache(const struct http_cache &) = delete;

    ~http_cache();
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
    LinkedBackgroundJob *job = (LinkedBackgroundJob *)ctx;

    if (error != nullptr) {
        cache_log(2, "http-cache: put failed: %s\n", error->message);
        g_error_free(error);
    }

    job->Remove();
}

static void
http_cache_put(HttpCacheRequest *request,
               unsigned rubber_id, size_t size)
{
    assert(request != nullptr);
    assert(request->info != nullptr);

    cache_log(4, "http_cache: put %s\n", request->key);

    if (request->cache->heap.IsDefined())
        request->cache->heap.Put(request->key,
                                 *request->info, request->headers,
                                 request->response.status,
                                 request->response.headers,
                                 *request->cache->rubber, rubber_id, size);
    else if (request->cache->memcached_stock != nullptr) {
        auto job = NewFromPool<LinkedBackgroundJob>(*request->pool,
                                                    request->cache->background);

        struct istream *value = rubber_id != 0
            ? istream_rubber_new(request->pool, request->cache->rubber,
                                 rubber_id, 0, size, true)
            : nullptr;

        http_cache_memcached_put(request->pool, request->cache->memcached_stock,
                                 &request->cache->pool,
                                 request->cache->background,
                                 request->key,
                                 request->info,
                                 request->headers,
                                 request->response.status, request->response.headers,
                                 value,
                                 http_cache_memcached_put_callback, job,
                                 request->cache->background.Add2(*job));
    }
}

static void
http_cache_remove(struct http_cache *cache, const char *url,
                  struct http_cache_document *document)
{
    if (cache->heap.IsDefined())
        cache->heap.Remove(url, *document);
}

static void
http_cache_remove_url(struct http_cache *cache, const char *url,
                      struct strmap *headers)
{
    if (cache->heap.IsDefined())
        cache->heap.RemoveURL(url, headers);
    else if (cache->memcached_stock != nullptr)
        http_cache_memcached_remove_uri_match(cache->memcached_stock,
                                              &cache->pool, cache->background,
                                              url, headers);
}

static void
http_cache_lock(struct http_cache &cache,
                struct http_cache_document &document)
{
    cache.heap.Lock(document);
}

static void
http_cache_unlock(struct http_cache *cache,
                  struct http_cache_document *document)
{
    cache->heap.Unlock(*document);
}

static void
http_cache_serve(HttpCacheRequest *request);

/*
 * sink_rubber handler
 *
 */

static void
http_cache_rubber_done(unsigned rubber_id, size_t size, void *ctx)
{
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

    request->RubberStoreFinished();

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(request, rubber_id, size);
}

static void
http_cache_rubber_oom(void *ctx)
{
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

    cache_log(4, "http_cache: oom %s\n", request->key);

    request->RubberStoreFinished();
}

static void
http_cache_rubber_too_large(void *ctx)
{
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

    cache_log(4, "http_cache: too large %s\n", request->key);

    request->RubberStoreFinished();
}

static void
http_cache_rubber_error(GError *error, void *ctx)
{
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

    cache_log(4, "http_cache: body_abort %s: %s\n",
              request->key, error->message);
    g_error_free(error);

    request->RubberStoreFinished();
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
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;
    struct http_cache *cache = request->cache;
    struct http_cache_document *locked_document =
        cache->heap.IsDefined() ? request->document : nullptr;

    if (request->document != nullptr && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == nullptr);

        cache_log(5, "http_cache: not_modified %s\n", request->key);
        http_cache_serve(request);
        pool_unref_denotify(request->caller_pool,
                            &request->caller_pool_notify);

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

        http_cache_serve(request);
        pool_unref_denotify(request->caller_pool,
                            &request->caller_pool_notify);

        if (locked_document != nullptr)
            http_cache_unlock(cache, locked_document);

        return;
    }

    request->operation.Finished();

    if (request->document != nullptr)
        http_cache_remove(request->cache, request->key, request->document);

    if (request->document != nullptr &&
        !cache->heap.IsDefined() &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    const off_t available = body != nullptr
        ? istream_available(body, true)
        : 0;

    if (!http_cache_response_evaluate(request->info,
                                      status, headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", request->key);

        request->handler.InvokeResponse(status, headers, body);
        pool_unref_denotify(request->caller_pool,
                            &request->caller_pool_notify);
        return;
    }

    request->response.status = status;
    request->response.headers = headers != nullptr
        ? strmap_dup(request->pool, headers)
        : nullptr;

    struct istream *const input = body;
    if (body == nullptr) {
        http_cache_put(request, 0, 0);
    } else {
        /* request->info was allocated from the caller pool; duplicate
           it to keep it alive even after the caller pool is
           destroyed */
        request->key = p_strdup(request->pool, request->key);
        request->info = http_cache_info_dup(*request->pool, *request->info);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(request->pool, body, false, false);

        request->cache->requests.push_front(*request);

        sink_rubber_new(request->pool, istream_tee_second(body),
                        cache->rubber, cacheable_size_limit,
                        &http_cache_rubber_handler, request,
                        &request->async_ref);
    }

    request->handler.InvokeResponse(status, headers, body);
    pool_unref_denotify(request->caller_pool,
                        &request->caller_pool_notify);

    if (input != nullptr && request->async_ref.IsDefined())
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_read(input);
}

static void
http_cache_response_abort(GError *error, void *ctx)
{
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

    g_prefix_error(&error, "http_cache %s: ", request->key);

    if (request->document != nullptr &&
        request->cache->heap.IsDefined())
        http_cache_unlock(request->cache, request->document);

    if (request->document != nullptr &&
        !request->cache->heap.IsDefined() &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    request->operation.Finished();
    request->handler.InvokeAbort(error);
    pool_unref_denotify(request->caller_pool,
                        &request->caller_pool_notify);
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
    auto request = HttpCacheRequest::FromAsync(ao);

    if (request->document != nullptr &&
        request->cache->heap.IsDefined())
        http_cache_unlock(request->cache, request->document);

    if (request->document != nullptr &&
        !request->cache->heap.IsDefined() &&
        request->document_body != nullptr)
        /* free the cached document istream (memcached) */
        istream_close_unused(request->document_body);

    pool_unref_denotify(request->caller_pool,
                        &request->caller_pool_notify);

    request->async_ref.Abort();
}

static const struct async_operation_class http_cache_async_operation = {
    .abort = http_cache_abort,
};


/*
 * constructor and public methods
 *
 */

HttpCacheRequest::HttpCacheRequest(struct pool &_pool,
                                   struct pool &_caller_pool,
                                   unsigned _session_sticky,
                                   struct http_cache &_cache,
                                   http_method_t _method,
                                   const struct resource_address &_address,
                                   const char *_key,
                                   struct strmap *_headers,
                                   const struct http_response_handler &_handler,
                                   void *_handler_ctx,
                                   struct http_cache_info &_info,
                                   struct async_operation_ref &_async_ref)
    :pool(&_pool), caller_pool(&_caller_pool),
     session_sticky(_session_sticky),
     cache(&_cache),
     method(_method),
     address(resource_address_dup(_pool, &_address)),
     key(_key),
     headers(_headers),
     info(&_info), document(nullptr) {
    handler.Set(_handler, _handler_ctx);
    operation.Init(http_cache_async_operation);
    _async_ref.Set(operation);
    pool_ref_notify(caller_pool, &caller_pool_notify);
}

inline
http_cache::http_cache(struct pool &_pool, size_t max_size,
                       struct memcached_stock *_memcached_stock,
                       struct resource_loader &_resource_loader)
    :pool(_pool),
     memcached_stock(_memcached_stock),
     resource_loader(_resource_loader)
{
    if (memcached_stock != nullptr || max_size > 0) {
        static const size_t max_memcached_rubber = 64 * 1024 * 1024;
        size_t rubber_size = max_size;
        if (memcached_stock != nullptr && rubber_size > max_memcached_rubber)
            rubber_size = max_memcached_rubber;

        rubber = rubber_new(rubber_size);
        if (rubber == nullptr) {
            fprintf(stderr, "Failed to allocate HTTP cache: %s\n",
                    strerror(errno));
            exit(2);
        }
    }

    if (memcached_stock == nullptr && max_size > 0)
        /* leave 12.5% of the rubber allocator empty, to increase the
           chances that a hole can be found for a new allocation, to
           reduce the pressure that rubber_compress() creates */
        heap.Init(pool, max_size * 7 / 8);
    else
        heap.Clear();
}

struct http_cache *
http_cache_new(struct pool *pool, size_t max_size,
               struct memcached_stock *memcached_stock,
               struct resource_loader *resource_loader)
{
    pool = pool_new_libc(pool, "http_cache");

    return NewFromPool<http_cache>(*pool, *pool, max_size,
                                   memcached_stock, *resource_loader);
}

void
HttpCacheRequest::RubberStoreFinished()
{
    assert(async_ref.IsDefined());

    async_ref.Clear();
    cache->requests.erase(cache->requests.iterator_to(*this));
}

void
HttpCacheRequest::AbortRubberStore()
{
    cache->requests.erase(cache->requests.iterator_to(*this));
    async_ref.Abort();
}

inline
http_cache::~http_cache()
{
    requests.clear_and_dispose(std::mem_fn(&HttpCacheRequest::AbortRubberStore));

    background.AbortAll();

    if (heap.IsDefined())
        heap.Deinit();

    rubber_free(rubber);
}

void
http_cache_close(struct http_cache *cache)
{
    DeleteUnrefTrashPool(cache->pool, cache);
}

void
http_cache_fork_cow(struct http_cache *cache, bool inherit)
{
    if (cache->heap.IsDefined() ||
        cache->memcached_stock != nullptr)
        rubber_fork_cow(cache->rubber, inherit);
}

void
http_cache_get_stats(const struct http_cache *cache, struct cache_stats *data)
{
    if (cache->heap.IsDefined())
        cache->heap.GetStats(*cache->rubber, *data);
    else
        data->Clear();
}

static void
http_cache_flush_callback(bool success, GError *error, void *ctx)
{
    LinkedBackgroundJob *flush = (LinkedBackgroundJob *)ctx;

    flush->Remove();

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
    if (cache->heap.IsDefined())
        cache->heap.Flush();
    else if (cache->memcached_stock != nullptr) {
        struct pool *pool = pool_new_linear(&cache->pool,
                                            "http_cache_memcached_flush", 1024);
        auto flush = NewFromPool<LinkedBackgroundJob>(*pool, cache->background);

        http_cache_memcached_flush(pool, cache->memcached_stock,
                                   http_cache_flush_callback, flush,
                                   cache->background.Add2(*flush));
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
    if (info->only_if_cached) {
        handler->InvokeResponse(handler_ctx, HTTP_STATUS_GATEWAY_TIMEOUT,
                                nullptr, nullptr);
        return;
    }

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    struct pool *pool = pool_new_linear(&cache->pool, "HttpCacheRequest",
                                        8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, *caller_pool,
                                      session_sticky, *cache,
                                      method, *address,
                                      http_cache_key(pool, address),
                                      headers == nullptr ? nullptr : strmap_dup(pool, headers),
                                      *handler, handler_ctx,
                                      *info, *async_ref);

    cache_log(4, "http_cache: miss %s\n", request->key);

    resource_loader_request(&cache->resource_loader, pool, session_sticky,
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
    cache_log(4, "http_cache: serve %s\n", key);

    struct http_response_handler_ref handler_ref;
    handler_ref.Set(*handler, handler_ctx);

    struct istream *response_body = cache->OpenStream(*pool, *document);

    handler_ref.InvokeResponse(document->status, document->response_headers,
                               response_body);
}

/**
 * Send the cached document to the caller (memcached version).
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_memcached_serve(HttpCacheRequest *request)
{
    cache_log(4, "http_cache: serve %s\n", request->key);

    request->operation.Finished();
    request->handler.InvokeResponse(request->document->status,
                                    request->document->response_headers,
                                    request->document_body);
}

/**
 * Send the cached document to the caller.
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_serve(HttpCacheRequest *request)
{
    if (request->cache->heap.IsDefined())
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
http_cache_test(HttpCacheRequest *request,
                http_method_t method,
                const struct resource_address *address,
                struct strmap *headers)
{
    struct http_cache *cache = request->cache;
    struct http_cache_document *document = request->document;

    cache_log(4, "http_cache: test %s\n", request->key);

    if (headers == nullptr)
        headers = strmap_new(request->pool);

    if (document->info.last_modified != nullptr)
        headers->Set("if-modified-since",
                     document->info.last_modified);

    if (document->info.etag != nullptr)
        headers->Set("if-none-match", document->info.etag);

    resource_loader_request(&cache->resource_loader, request->pool,
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
    struct pool *pool = pool_new_linear(&cache->pool, "HttpCacheRequest", 8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, *caller_pool,
                                      session_sticky, *cache,
                                      method, *address,
                                      http_cache_key(pool, address),
                                      headers == nullptr ? nullptr : strmap_dup(pool, headers),
                                      *handler, handler_ctx,
                                      *info, *async_ref);

    http_cache_lock(*cache, *document);
    request->document = document;

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
        = cache->heap.Get(http_cache_key(pool, address), headers);

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
http_cache_memcached_forward(HttpCacheRequest *request,
                             const struct http_response_handler *handler,
                             void *handler_ctx)
{
    resource_loader_request(&request->cache->resource_loader, request->pool,
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
http_cache_memcached_miss(HttpCacheRequest *request)
{
    struct http_cache_info *info = request->info;

    if (info->only_if_cached) {
        request->operation.Finished();
        request->handler.InvokeResponse(HTTP_STATUS_GATEWAY_TIMEOUT,
                                        nullptr, nullptr);

        pool_unref_denotify(request->caller_pool,
                            &request->caller_pool_notify);
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
    HttpCacheRequest *request = (HttpCacheRequest *)ctx;

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

        request->operation.Finished();
        request->handler.InvokeResponse(document->status,
                                        document->response_headers,
                                        body);
        pool_unref_denotify(request->caller_pool,
                            &request->caller_pool_notify);
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
    assert(cache->memcached_stock != nullptr);

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    struct pool *pool = pool_new_linear(&cache->pool, "HttpCacheRequest",
                                        8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, *caller_pool,
                                      session_sticky, *cache,
                                      method, *address,
                                      http_cache_key(pool, address),
                                      headers == nullptr ? nullptr : strmap_dup(pool, headers),
                                      *handler, handler_ctx,
                                      *info, *async_ref);

    http_cache_memcached_get(pool, cache->memcached_stock,
                             &request->cache->pool,
                             cache->background,
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
    const char *key = cache->heap.IsDefined() ||
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
        resource_loader_request(&cache->resource_loader, pool, session_sticky,
                                method, address,
                                HTTP_STATUS_OK, headers, body,
                                handler, handler_ctx,
                                async_ref);
        return;
    }

    struct http_cache_info *info =
        http_cache_request_evaluate(*pool, method, *address, headers, body);
    if (info != nullptr) {
        assert(body == nullptr);

        if (cache->heap.IsDefined())
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

        resource_loader_request(&cache->resource_loader, pool, session_sticky,
                                method, address,
                                HTTP_STATUS_OK, headers, body,
                                handler, handler_ctx,
                                async_ref);
    }
}
