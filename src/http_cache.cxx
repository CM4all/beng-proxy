/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "http_cache_internal.hxx"
#include "http_cache_document.hxx"
#include "http_cache_rfc.hxx"
#include "http_cache_heap.hxx"
#include "http_cache_memcached.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "http_util.hxx"
#include "cache.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "AllocatorStats.hxx"
#include "istream_rubber.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_tee.hxx"
#include "AllocatorPtr.hxx"
#include "event/TimerEvent.hxx"
#include "util/Background.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"

#include <boost/intrusive/list.hpp>

#include <functional>

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

static constexpr struct timeval http_cache_compress_interval = { 600, 0 };

class HttpCacheRequest final : public HttpResponseHandler,
                               public RubberSinkHandler,
                               Cancellable {
public:
    static constexpr auto link_mode = boost::intrusive::normal_link;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

    struct pool &pool, &caller_pool;

#ifndef NDEBUG
    struct pool_notify_state caller_pool_notify;
#endif

    sticky_hash_t session_sticky;

    /**
     * The cache object which got this request.
     */
    HttpCache &cache;
    http_method_t method;
    const ResourceAddress address;

    /**
     * The cache key used to address the associated cache document.
     */
    const char *key;

    /** headers from the original request */
    StringMap headers;

    HttpResponseHandler &handler;

    HttpCacheRequestInfo request_info;

    /**
     * Information on the request passed to http_cache_request().
     */
    HttpCacheResponseInfo info;

    /**
     * The document which was found in the cache, in case this is a
     * request to test the validity of the cache entry.  If this is
     * nullptr, then we had a cache miss.
     */
    HttpCacheDocument *document = nullptr;

    /**
     * The response body from the http_cache_document.  This is not
     * used for the heap backend: it creates the #istream on demand
     * with http_cache_heap_istream().
     */
    Istream *document_body;

    /**
     * This struct holds response information while this module
     * receives the response body.
     */
    struct {
        http_status_t status;
        StringMap *headers;
    } response;

    CancellablePointer cancel_ptr;

    HttpCacheRequest(struct pool &_pool, struct pool &_caller_pool,
                     sticky_hash_t _session_sticky,
                     HttpCache &_cache,
                     http_method_t _method,
                     const ResourceAddress &_address,
                     const char *_key,
                     const StringMap &_headers,
                     HttpResponseHandler &_handler,
                     HttpCacheRequestInfo &_info,
                     CancellablePointer &_cancel_ptr);

    HttpCacheRequest(const HttpCacheRequest &) = delete;
    HttpCacheRequest &operator=(const HttpCacheRequest &) = delete;

    /**
     * Storing the response body in the rubber allocator has finished
     * (but may have failed).
     */
    void RubberStoreFinished();

    /**
     * Abort storing the response body in the rubber allocator.
     */
    void AbortRubberStore();

    /* virtual methods from class Cancellable */
    void Cancel() override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(std::exception_ptr ep) override;

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(std::exception_ptr ep) override;
};

class HttpCache {
public:
    struct pool &pool;

    EventLoop &event_loop;

    TimerEvent compress_timer;

    Rubber *rubber = nullptr;

    HttpCacheHeap heap;

    MemachedStock *memcached_stock;

    ResourceLoader &resource_loader;

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

    HttpCache(struct pool &_pool, size_t max_size,
              MemachedStock *_memcached_stock,
              EventLoop &event_loop,
              ResourceLoader &_resource_loader);

    HttpCache(const HttpCache &) = delete;
    HttpCache &operator=(const HttpCache &) = delete;

    ~HttpCache();

private:
    void OnCompressTimer() {
        rubber_compress(rubber);
        if (heap.IsDefined())
            heap.Compress();
        compress_timer.Add(http_cache_compress_interval);
    }
};

static const char *
http_cache_key(struct pool &pool, const ResourceAddress &address)
{
    switch (address.type) {
    case ResourceAddress::Type::NONE:
    case ResourceAddress::Type::LOCAL:
    case ResourceAddress::Type::PIPE:
        /* not cacheable */
        return nullptr;

    case ResourceAddress::Type::HTTP:
    case ResourceAddress::Type::LHTTP:
    case ResourceAddress::Type::CGI:
    case ResourceAddress::Type::FASTCGI:
    case ResourceAddress::Type::WAS:
    case ResourceAddress::Type::NFS:
        return address.GetId(pool);
    }

    /* unreachable */
    assert(false);
    return nullptr;
}

static void
http_cache_memcached_put_callback(std::exception_ptr ep, void *ctx)
{
    LinkedBackgroundJob *job = (LinkedBackgroundJob *)ctx;

    if (ep)
        cache_log(2, "http-cache: put failed: %s\n", GetFullMessage(ep).c_str());

    job->Remove();
}

static void
http_cache_put(HttpCacheRequest &request,
               unsigned rubber_id, size_t size)
{
    cache_log(4, "http_cache: put %s\n", request.key);

    if (request.cache.heap.IsDefined())
        request.cache.heap.Put(request.key,
                               request.info, request.headers,
                               request.response.status,
                               *request.response.headers,
                               *request.cache.rubber, rubber_id, size);
    else if (request.cache.memcached_stock != nullptr) {
        auto job = NewFromPool<LinkedBackgroundJob>(request.pool,
                                                    request.cache.background);

        Istream *value = rubber_id != 0
            ? istream_rubber_new(request.pool, *request.cache.rubber,
                                 rubber_id, 0, size, true)
            : nullptr;

        http_cache_memcached_put(request.pool,
                                 *request.cache.memcached_stock,
                                 request.cache.pool,
                                 request.cache.background,
                                 request.key,
                                 request.info,
                                 request.headers,
                                 request.response.status, request.response.headers,
                                 value,
                                 http_cache_memcached_put_callback, job,
                                 request.cache.background.Add2(*job));
    }
}

static void
http_cache_remove(HttpCache &cache, HttpCacheDocument *document)
{
    if (cache.heap.IsDefined())
        cache.heap.Remove(*document);
}

static void
http_cache_remove_url(HttpCache &cache, const char *url,
                      StringMap &headers)
{
    if (cache.heap.IsDefined())
        cache.heap.RemoveURL(url, headers);
    else if (cache.memcached_stock != nullptr)
        http_cache_memcached_remove_uri_match(*cache.memcached_stock,
                                              cache.pool, cache.background,
                                              url, headers);
}

static void
http_cache_lock(HttpCache &cache,
                HttpCacheDocument &document)
{
    cache.heap.Lock(document);
}

static void
http_cache_unlock(HttpCache &cache,
                  HttpCacheDocument *document)
{
    cache.heap.Unlock(*document);
}

static void
http_cache_serve(HttpCacheRequest &equest);

/*
 * sink_rubber handler
 *
 */

void
HttpCacheRequest::RubberDone(unsigned rubber_id, size_t size)
{
    RubberStoreFinished();

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    http_cache_put(*this, rubber_id, size);
}

void
HttpCacheRequest::RubberOutOfMemory()
{
    cache_log(4, "http_cache: nocache oom %s\n", key);

    RubberStoreFinished();
}

void
HttpCacheRequest::RubberTooLarge()
{
    cache_log(4, "http_cache: nocache too large %s\n", key);

    RubberStoreFinished();
}

void
HttpCacheRequest::RubberError(std::exception_ptr ep)
{
    cache_log(4, "http_cache: body_abort %s: %s\n",
              key, GetFullMessage(ep).c_str());

    RubberStoreFinished();
}

/*
 * http response handler
 *
 */

void
HttpCacheRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
                                 Istream *body)
{
    HttpCacheDocument *locked_document = cache.heap.IsDefined()
        ? document
        : nullptr;

    if (document != nullptr && status == HTTP_STATUS_NOT_MODIFIED) {
        assert(body == nullptr);

        cache_log(5, "http_cache: not_modified %s\n", key);
        http_cache_serve(*this);
        pool_unref_denotify(&caller_pool,
                            &caller_pool_notify);

        if (locked_document != nullptr)
            http_cache_unlock(cache, locked_document);

        return;
    }

    if (document != nullptr &&
        http_cache_prefer_cached(*document, _headers)) {
        cache_log(4, "http_cache: matching etag '%s' for %s, using cache entry\n",
                  document->info.etag, key);

        if (body != nullptr)
            body->CloseUnused();

        http_cache_serve(*this);
        pool_unref_denotify(&caller_pool, &caller_pool_notify);

        if (locked_document != nullptr)
            http_cache_unlock(cache, locked_document);

        return;
    }

    if (document != nullptr)
        http_cache_remove(cache, document);

    if (document != nullptr &&
        !cache.heap.IsDefined() &&
        document_body != nullptr)
        /* free the cached document istream (memcached) */
        document_body->CloseUnused();

    const off_t available = body != nullptr
        ? body->GetAvailable(true)
        : 0;

    if (!http_cache_response_evaluate(request_info, info,
                                      status, _headers, available)) {
        /* don't cache response */
        cache_log(4, "http_cache: nocache %s\n", key);

        handler.InvokeResponse(status, std::move(_headers), body);
        pool_unref_denotify(&caller_pool,
                            &caller_pool_notify);
        return;
    }

    response.status = status;
    response.headers = strmap_dup(&pool, &_headers);

    Istream *const input = body;
    if (body == nullptr) {
        http_cache_put(*this, 0, 0);
    } else {
        /* this->info was allocated from the caller pool; duplicate
           it to keep it alive even after the caller pool is
           destroyed */
        key = p_strdup(&pool, key);
        info.MoveToPool(pool);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(pool, *body,
                               cache.event_loop,
                               false, false);

        cache.requests.push_front(*this);

        sink_rubber_new(pool, istream_tee_second(*body),
                        *cache.rubber, cacheable_size_limit,
                        *this,
                        cancel_ptr);
    }

    handler.InvokeResponse(status, std::move(_headers), body);
    pool_unref_denotify(&caller_pool, &caller_pool_notify);

    if (input != nullptr && cancel_ptr)
        /* just in case our handler has closed the body without
           looking at it: call istream_read() to start reading */
        input->Read();
}

void
HttpCacheRequest::OnHttpError(std::exception_ptr ep)
{
    ep = NestException(ep, FormatRuntimeError("http_cache %s", key));

    if (document != nullptr && cache.heap.IsDefined())
        http_cache_unlock(cache, document);

    if (document != nullptr &&
        !cache.heap.IsDefined() &&
        document_body != nullptr)
        /* free the cached document istream (memcached) */
        document_body->CloseUnused();

    handler.InvokeError(ep);
    pool_unref_denotify(&caller_pool, &caller_pool_notify);
}

/*
 * async operation
 *
 */

void
HttpCacheRequest::Cancel()
{
    if (document != nullptr && cache.heap.IsDefined())
        http_cache_unlock(cache, document);

    if (document != nullptr &&
        !cache.heap.IsDefined() &&
        document_body != nullptr)
        /* free the cached document istream (memcached) */
        document_body->CloseUnused();

    pool_unref_denotify(&caller_pool, &caller_pool_notify);

    cancel_ptr.Cancel();
}


/*
 * constructor and public methods
 *
 */

HttpCacheRequest::HttpCacheRequest(struct pool &_pool,
                                   struct pool &_caller_pool,
                                   sticky_hash_t _session_sticky,
                                   HttpCache &_cache,
                                   http_method_t _method,
                                   const ResourceAddress &_address,
                                   const char *_key,
                                   const StringMap &_headers,
                                   HttpResponseHandler &_handler,
                                   HttpCacheRequestInfo &_request_info,
                                   CancellablePointer &_cancel_ptr)
    :pool(_pool), caller_pool(_caller_pool),
     session_sticky(_session_sticky),
     cache(_cache),
     method(_method),
     address(_pool, _address),
     key(_key),
     headers(_pool, _headers),
     handler(_handler),
     request_info(_request_info) {
    _cancel_ptr = *this;
    pool_ref_notify(&caller_pool, &caller_pool_notify);
}

inline
HttpCache::HttpCache(struct pool &_pool, size_t max_size,
                     MemachedStock *_memcached_stock,
                     EventLoop &_event_loop,
                     ResourceLoader &_resource_loader)
    :pool(*pool_new_libc(&_pool, "http_cache")),
     event_loop(_event_loop),
     compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
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

        compress_timer.Add(http_cache_compress_interval);
    }

    if (memcached_stock == nullptr && max_size > 0)
        /* leave 12.5% of the rubber allocator empty, to increase the
           chances that a hole can be found for a new allocation, to
           reduce the pressure that rubber_compress() creates */
        heap.Init(pool, event_loop, max_size * 7 / 8);
    else
        heap.Clear();
}

HttpCache *
http_cache_new(struct pool &pool, size_t max_size,
               MemachedStock *memcached_stock,
               EventLoop &event_loop,
               ResourceLoader &resource_loader)
{
    return new HttpCache(pool, max_size,
                         memcached_stock, event_loop, resource_loader);
}

void
HttpCacheRequest::RubberStoreFinished()
{
    assert(cancel_ptr);

    cancel_ptr = nullptr;
    cache.requests.erase(cache.requests.iterator_to(*this));
}

void
HttpCacheRequest::AbortRubberStore()
{
    cache.requests.erase(cache.requests.iterator_to(*this));
    cancel_ptr.Cancel();
}

inline
HttpCache::~HttpCache()
{
    requests.clear_and_dispose(std::mem_fn(&HttpCacheRequest::AbortRubberStore));

    background.AbortAll();

    if (heap.IsDefined())
        heap.Deinit();

    compress_timer.Cancel();

    if (rubber != nullptr)
        rubber_free(rubber);

    pool_unref(&pool);
}

void
http_cache_close(HttpCache *cache)
{
    delete cache;
}

void
http_cache_fork_cow(HttpCache &cache, bool inherit)
{
    if (cache.heap.IsDefined() ||
        cache.memcached_stock != nullptr)
        rubber_fork_cow(cache.rubber, inherit);

    if (cache.heap.IsDefined())
        cache.heap.ForkCow(inherit);
}

AllocatorStats
http_cache_get_stats(const HttpCache &cache)
{
    return cache.heap.IsDefined()
        ? cache.heap.GetStats(*cache.rubber)
        : AllocatorStats::Zero();
}

static void
http_cache_flush_callback(bool success, std::exception_ptr ep, void *ctx)
{
    LinkedBackgroundJob *flush = (LinkedBackgroundJob *)ctx;

    flush->Remove();

    if (success)
        cache_log(5, "http_cache_memcached: flushed\n");
    else if (ep)
        cache_log(5, "http_cache_memcached: flush has failed: %s\n",
                  GetFullMessage(ep).c_str());
    else
        cache_log(5, "http_cache_memcached: flush has failed\n");
}

void
http_cache_flush(HttpCache &cache)
{
    if (cache.heap.IsDefined())
        cache.heap.Flush();
    else if (cache.memcached_stock != nullptr) {
        struct pool *pool = pool_new_linear(&cache.pool,
                                            "http_cache_memcached_flush", 1024);
        auto flush = NewFromPool<LinkedBackgroundJob>(*pool, cache.background);

        http_cache_memcached_flush(*pool, *cache.memcached_stock,
                                   http_cache_flush_callback, flush,
                                   cache.background.Add2(*flush));
        pool_unref(pool);
    }

    if (cache.rubber != nullptr)
        rubber_compress(cache.rubber);
}

/**
 * A resource was not found in the cache.
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_miss(HttpCache &cache, struct pool &caller_pool,
                sticky_hash_t session_sticky,
                HttpCacheRequestInfo &info,
                http_method_t method,
                const ResourceAddress &address,
                StringMap &&headers,
                HttpResponseHandler &handler,
                CancellablePointer &cancel_ptr)
{
    if (info.only_if_cached) {
        handler.InvokeResponse(HTTP_STATUS_GATEWAY_TIMEOUT,
                               StringMap(caller_pool), nullptr);
        return;
    }

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache.pool */
    struct pool *pool = pool_new_linear(&cache.pool, "HttpCacheRequest",
                                        8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, caller_pool,
                                      session_sticky, cache,
                                      method, address,
                                      http_cache_key(*pool, address),
                                      headers,
                                      handler,
                                      info, cancel_ptr);

    cache_log(4, "http_cache: miss %s\n", request->key);

    cache.resource_loader.SendRequest(*pool, session_sticky,
                                      method, address,
                                      HTTP_STATUS_OK, std::move(headers),
                                      nullptr, nullptr,
                                      *request,
                                      request->cancel_ptr);
    pool_unref(pool);
}

/**
 * Send the cached document to the caller (heap version).
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_heap_serve(HttpCacheHeap &cache,
                      HttpCacheDocument &document,
                      struct pool &pool,
                      const char *key gcc_unused,
                      HttpResponseHandler &handler)
{
    cache_log(4, "http_cache: serve %s\n", key);

    Istream *response_body = cache.OpenStream(pool, document);

    handler.InvokeResponse(document.status,
                           StringMap(ShallowCopy(), pool,
                                     document.response_headers),
                           response_body);
}

/**
 * Send the cached document to the caller (memcached version).
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_memcached_serve(HttpCacheRequest &request)
{
    cache_log(4, "http_cache: serve %s\n", request.key);

    request.handler.InvokeResponse(request.document->status,
                                   StringMap(ShallowCopy(), request.caller_pool,
                                             request.document->response_headers),
                                   request.document_body);
}

/**
 * Send the cached document to the caller.
 *
 * Caller pool is left unchanged.
 */
static void
http_cache_serve(HttpCacheRequest &request)
{
    if (request.cache.heap.IsDefined())
        http_cache_heap_serve(request.cache.heap, *request.document,
                              request.pool, request.key,
                              request.handler);
    else if (request.cache.memcached_stock != nullptr)
        http_cache_memcached_serve(request);
}

/**
 * Revalidate a cache entry.
 *
 * Caller pool is freed asynchronously.
 */
static void
http_cache_test(HttpCacheRequest &request,
                http_method_t method,
                const ResourceAddress &address,
                StringMap &&headers)
{
    HttpCache &cache = request.cache;
    HttpCacheDocument &document = *request.document;

    cache_log(4, "http_cache: test %s\n", request.key);

    if (document.info.last_modified != nullptr)
        headers.Set("if-modified-since", document.info.last_modified);

    if (document.info.etag != nullptr)
        headers.Set("if-none-match", document.info.etag);

    cache.resource_loader.SendRequest(request.pool,
                                      request.session_sticky,
                                      method, address,
                                      HTTP_STATUS_OK, std::move(headers),
                                      nullptr, nullptr,
                                      request,
                                      request.cancel_ptr);
}

/**
 * Revalidate a cache entry (heap version).
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_heap_test(HttpCache &cache, struct pool &caller_pool,
                     sticky_hash_t session_sticky,
                     HttpCacheRequestInfo &info,
                     HttpCacheDocument &document,
                     http_method_t method,
                     const ResourceAddress &address,
                     StringMap &&headers,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache.pool */
    struct pool *pool = pool_new_linear(&cache.pool, "HttpCacheRequest", 8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, caller_pool,
                                      session_sticky, cache,
                                      method, address,
                                      http_cache_key(*pool, address),
                                      headers,
                                      handler,
                                      info, cancel_ptr);

    http_cache_lock(cache, document);
    request->document = &document;

    http_cache_test(*request, method, address, std::move(headers));
    pool_unref(pool);
}

static bool
http_cache_may_serve(HttpCacheRequestInfo &info,
                     const HttpCacheDocument &document)
{
    return info.only_if_cached ||
        document.info.expires >= std::chrono::system_clock::now();
}

/**
 * The requested document was found in the cache.  It is either served
 * or revalidated.
 *
 * Caller pool is referenced synchronously and freed asynchronously
 * (as needed).
 */
static void
http_cache_found(HttpCache &cache,
                 HttpCacheRequestInfo &info,
                 HttpCacheDocument &document,
                 struct pool &pool,
                 sticky_hash_t session_sticky,
                 http_method_t method,
                 const ResourceAddress &address,
                 StringMap &&headers,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr)
{
    if (http_cache_may_serve(info, document))
        http_cache_heap_serve(cache.heap, document, pool,
                              http_cache_key(pool, address),
                              handler);
    else
        http_cache_heap_test(cache, pool, session_sticky, info, document,
                             method, address, std::move(headers),
                             handler, cancel_ptr);
}

/**
 * Query the heap cache.
 *
 * Caller pool is referenced synchronously and freed asynchronously
 * (as needed).
 */
static void
http_cache_heap_use(HttpCache &cache,
                    struct pool &pool, sticky_hash_t session_sticky,
                    http_method_t method,
                    const ResourceAddress &address,
                    StringMap &&headers,
                    HttpCacheRequestInfo &info,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr)
{
    HttpCacheDocument *document =
        cache.heap.Get(http_cache_key(pool, address), headers);

    if (document == nullptr)
        http_cache_miss(cache, pool, session_sticky, info,
                        method, address, std::move(headers),
                        handler, cancel_ptr);
    else
        http_cache_found(cache, info, *document, pool, session_sticky,
                         method, address, std::move(headers),
                         handler, cancel_ptr);
}

/**
 * Forward the HTTP request to the real server.
 *
 * Caller pool is freed asynchronously.
 */
static void
http_cache_memcached_forward(HttpCacheRequest &request,
                             HttpResponseHandler &handler)
{
    request.cache.resource_loader.SendRequest(request.pool,
                                              request.session_sticky,
                                              request.method, request.address,
                                              HTTP_STATUS_OK,
                                              StringMap(ShallowCopy(),
                                                        request.pool,
                                                        request.headers),
                                              nullptr, nullptr,
                                              handler, request.cancel_ptr);
}

/**
 * A resource was not found in the cache.
 *
 * Caller pool is freed (asynchronously).
 */
static void
http_cache_memcached_miss(HttpCacheRequest &request)
{
    if (request.request_info.only_if_cached) {
        request.handler.InvokeResponse(HTTP_STATUS_GATEWAY_TIMEOUT,
                                       StringMap(request.pool),
                                       nullptr);

        pool_unref_denotify(&request.caller_pool,
                            &request.caller_pool_notify);
        return;
    }

    cache_log(4, "http_cache: miss %s\n", request.key);

    request.document = nullptr;

    http_cache_memcached_forward(request, request);
}

/**
 * The memcached-client callback.
 *
 * Caller pool is freed (asynchronously).
 */
static void
http_cache_memcached_get_callback(HttpCacheDocument *document,
                                  Istream *body, std::exception_ptr ep,
                                  void *ctx)
{
    HttpCacheRequest &request = *(HttpCacheRequest *)ctx;

    if (document == nullptr) {
        if (ep)
            cache_log(2, "http_cache: get failed: %s\n", GetFullMessage(ep).c_str());

        http_cache_memcached_miss(request);
        return;
    }

    if (http_cache_may_serve(request.request_info, *document)) {
        cache_log(4, "http_cache: serve %s\n", request.key);

        request.handler.InvokeResponse(document->status,
                                       StringMap(ShallowCopy(), request.caller_pool,
                                                 document->response_headers),
                                       body);
        pool_unref_denotify(&request.caller_pool,
                            &request.caller_pool_notify);
    } else {
        request.document = document;
        request.document_body = istream_hold_new(request.pool, *body);

        http_cache_test(request, request.method, request.address,
                        StringMap(ShallowCopy(),
                                  request.pool,
                                  request.headers));
    }
}

/**
 * Query the resource from the memached server.
 *
 * Caller pool is referenced synchronously and freed asynchronously.
 */
static void
http_cache_memcached_use(HttpCache &cache,
                         struct pool &caller_pool,
                         sticky_hash_t session_sticky,
                         http_method_t method,
                         const ResourceAddress &address,
                         StringMap &headers,
                         HttpCacheRequestInfo &info,
                         HttpResponseHandler &handler,
                         CancellablePointer &cancel_ptr)
{
    assert(cache.memcached_stock != nullptr);

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache.pool */
    struct pool *pool = pool_new_linear(&cache.pool, "HttpCacheRequest",
                                        8192);

    auto request =
        NewFromPool<HttpCacheRequest>(*pool, *pool, caller_pool,
                                      session_sticky, cache,
                                      method, address,
                                      http_cache_key(*pool, address),
                                      headers,
                                      handler,
                                      info, cancel_ptr);

    http_cache_memcached_get(*pool, *cache.memcached_stock,
                             request->cache.pool,
                             cache.background,
                             request->key, request->headers,
                             http_cache_memcached_get_callback, request,
                             request->cancel_ptr);
    pool_unref(pool);
}

void
http_cache_request(HttpCache &cache,
                   struct pool &pool, sticky_hash_t session_sticky,
                   http_method_t method,
                   const ResourceAddress &address,
                   StringMap &&headers, Istream *body,
                   HttpResponseHandler &handler,
                   CancellablePointer &cancel_ptr)
{
    const char *key = cache.heap.IsDefined() ||
        cache.memcached_stock != nullptr
        ? http_cache_key(pool, address)
        : nullptr;
    if (/* this address type cannot be cached; skip the rest of this
           library */
        key == nullptr ||
        /* don't cache a huge request URI; probably it contains lots
           and lots of unique parameters, and that's not worth the
           cache space anyway */
        strlen(key) > 8192) {
        cache.resource_loader.SendRequest(pool, session_sticky,
                                          method, address,
                                          HTTP_STATUS_OK, std::move(headers),
                                          body, nullptr,
                                          handler, cancel_ptr);
        return;
    }

    HttpCacheRequestInfo info;
    if (http_cache_request_evaluate(info, method, address, headers, body)) {
        assert(body == nullptr);

        if (cache.heap.IsDefined())
            http_cache_heap_use(cache, pool, session_sticky,
                                method, address, std::move(headers), info,
                                handler, cancel_ptr);
        else if (cache.memcached_stock != nullptr)
            http_cache_memcached_use(cache, pool, session_sticky,
                                     method, address, headers, info,
                                     handler, cancel_ptr);
    } else {
        if (http_cache_request_invalidate(method))
            http_cache_remove_url(cache, key, headers);

        cache_log(4, "http_cache: ignore %s\n", key);

        cache.resource_loader.SendRequest(pool, session_sticky,
                                          method, address,
                                          HTTP_STATUS_OK, std::move(headers),
                                          body, nullptr,
                                          handler, cancel_ptr);
    }
}
