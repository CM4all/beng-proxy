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

#include "fcache.hxx"
#include "cache.hxx"
#include "http_request.hxx"
#include "header_writer.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "abort_unref.hxx"
#include "tpool.hxx"
#include "ResourceAddress.hxx"
#include "ResourceLoader.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_tee.hxx"
#include "istream_unlock.hxx"
#include "istream_rubber.hxx"
#include "rubber.hxx"
#include "SlicePool.hxx"
#include "sink_rubber.hxx"
#include "AllocatorStats.hxx"
#include "pool.hxx"
#include "event/TimerEvent.hxx"
#include "io/Logger.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"

#include <boost/intrusive/list.hpp>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

static constexpr off_t cacheable_size_limit = 512 * 1024;

/**
 * The timeout for the underlying HTTP request.  After this timeout
 * expires, the filter cache gives up and doesn't store the response.
 */
static constexpr struct timeval fcache_request_timeout = { 60, 0 };

static constexpr struct timeval fcache_compress_interval = { 600, 0 };

/**
 * The default "expires" duration [s] if no expiration was given for
 * the input.
 */
static constexpr std::chrono::seconds fcache_default_expires(7 * 24 * 3600);

struct FilterCacheInfo {
    /** when will the cached resource expire? (beng-proxy time) */
    std::chrono::system_clock::time_point expires;

    /** the final resource id */
    const char *key;

    FilterCacheInfo(const char *_key)
        :expires(std::chrono::system_clock::from_time_t(-1)), key(_key) {}

    FilterCacheInfo(struct pool &pool, const FilterCacheInfo &src)
        :expires(src.expires),
         key(p_strdup(&pool, src.key)) {}

    FilterCacheInfo(FilterCacheInfo &&src) = default;

    FilterCacheInfo &operator=(const FilterCacheInfo &) = delete;
};

struct FilterCacheItem final : CacheItem {
    struct pool &pool;

    const FilterCacheInfo info;

    const http_status_t status;
    StringMap headers;

    const size_t size;
    Rubber &rubber;
    const unsigned rubber_id;

    FilterCacheItem(struct pool &_pool, const FilterCacheInfo &_info,
                    http_status_t _status, const StringMap &_headers,
                    size_t _size, Rubber &_rubber, unsigned _rubber_id,
                    std::chrono::system_clock::time_point _expires)
        :CacheItem(_expires, pool_netto_size(&_pool) + _size),
         pool(_pool), info(pool, _info),
         status(_status), headers(pool, _headers),
         size(_size), rubber(_rubber), rubber_id(_rubber_id) {
    }

    /* virtual methods from class CacheItem */
    void Destroy() override {
        if (rubber_id != 0)
            rubber.Remove(rubber_id);

        DeleteUnrefTrashPool(pool, this);
    }

};

struct FilterCacheRequest final : HttpResponseHandler, RubberSinkHandler {
    static constexpr auto link_mode = boost::intrusive::auto_unlink;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

    struct pool &pool, &caller_pool;
    FilterCache &cache;
    HttpResponseHandler &handler;

    FilterCacheInfo info;

    struct {
        http_status_t status;
        StringMap *headers;

        /**
         * A handle to abort the sink_rubber that copies response body
         * data into a new rubber allocation.
         */
        CancellablePointer cancel_ptr;
    } response;

    /**
     * This event is initialized by the response callback, and limits
     * the duration for receiving the response body.
     */
    TimerEvent timeout_event;

    FilterCacheRequest(struct pool &_pool, struct pool &_caller_pool,
                       FilterCache &_cache,
                       HttpResponseHandler &_handler,
                       const FilterCacheInfo &_info);

    void OnTimeout();

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(std::exception_ptr ep) override;
};

class FilterCache {
    friend struct FilterCacheRequest;

    struct pool &pool;
    Cache cache;
    Rubber rubber;
    SlicePool *const slice_pool;

    TimerEvent compress_timer;

    EventLoop &event_loop;
    ResourceLoader &resource_loader;

    /**
     * A list of requests that are currently copying the response body
     * to a #Rubber allocation.  We keep track of them so we can
     * cancel them on shutdown.
     */
    boost::intrusive::list<FilterCacheRequest,
                           boost::intrusive::member_hook<FilterCacheRequest,
                                                         FilterCacheRequest::SiblingsHook,
                                                         &FilterCacheRequest::siblings>,
                           boost::intrusive::constant_time_size<false>> requests;

public:
    FilterCache(struct pool &_pool, size_t max_size,
                EventLoop &_event_loop, ResourceLoader &_resource_loader);

    ~FilterCache();

    void ForkCow(bool inherit) {
        rubber.ForkCow(inherit);
        slice_pool_fork_cow(*slice_pool, inherit);
    }

    AllocatorStats GetStats() const noexcept {
        return slice_pool_get_stats(*slice_pool)
            + rubber.GetStats();
    }

    void Flush() {
        cache.Flush();
        Compress();
    }

    void Get(struct pool &caller_pool,
             const ResourceAddress &address,
             const char *source_id,
             http_status_t status, StringMap &&headers,
             UnusedIstreamPtr body,
             HttpResponseHandler &handler,
             CancellablePointer &cancel_ptr);

    void Put(const FilterCacheInfo &info,
             http_status_t status, const StringMap &headers,
             unsigned rubber_id, size_t size);

private:
    void Miss(struct pool &caller_pool,
              FilterCacheInfo &&info,
              const ResourceAddress &address,
              http_status_t status, StringMap &&headers,
              UnusedIstreamPtr body, const char *body_etag,
              HttpResponseHandler &_handler,
              CancellablePointer &cancel_ptr);

    void Serve(FilterCacheItem &item,
               struct pool &caller_pool,
               HttpResponseHandler &handler);

    void Hit(FilterCacheItem &item,
             struct pool &caller_pool,
             HttpResponseHandler &handler);

    void Compress() {
        rubber.Compress();
        slice_pool_compress(slice_pool);
    }

    void OnCompressTimer() {
        Compress();
        compress_timer.Add(fcache_compress_interval);
    }
};

FilterCacheRequest::FilterCacheRequest(struct pool &_pool,
                                       struct pool &_caller_pool,
                                       FilterCache &_cache,
                                       HttpResponseHandler &_handler,
                                       const FilterCacheInfo &_info)
    :pool(_pool), caller_pool(_caller_pool),
     cache(_cache),
     handler(_handler),
     info(pool, _info),
     timeout_event(cache.event_loop, BIND_THIS_METHOD(OnTimeout)) {}

/**
 * Release resources held by this request.
 */
static void
filter_cache_request_release(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(!request->response.cancel_ptr);

    request->timeout_event.Cancel();

    /* unref but don't trash the pool; it may still be in use by our
       caller who may still be reading from the TeeIstream */
    DeleteUnrefPool(request->pool, request);
}

/**
 * Abort the request.
 */
static void
filter_cache_request_abort(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(request->response.cancel_ptr);

    request->response.cancel_ptr.CancelAndClear();
    filter_cache_request_release(request);
}

/* check whether the request could produce a cacheable response */
static FilterCacheInfo *
filter_cache_request_evaluate(struct pool &pool,
                              const ResourceAddress &address,
                              const char *source_id,
                              const StringMap &headers)
{
    if (source_id == nullptr)
        return nullptr;

    const char *user = headers.Get("x-cm4all-beng-user");
    if (user == nullptr)
        user = "";

    return NewFromPool<FilterCacheInfo>(pool,
                                        p_strcat(&pool, source_id, "|",
                                                 user, "|",
                                                 address.GetId(pool), nullptr));
}

void
FilterCache::Put(const FilterCacheInfo &info,
                 http_status_t status, const StringMap &headers,
                 unsigned rubber_id, size_t size)
{
    LogConcat(4, "FilterCache", "put ", info.key);

    std::chrono::system_clock::time_point expires;
    if (info.expires == std::chrono::system_clock::from_time_t(-1))
        expires = std::chrono::system_clock::now() + fcache_default_expires;
    else
        expires = info.expires;

    struct pool *item_pool = pool_new_slice(&pool, "FilterCacheItem",
                                            slice_pool);
    auto item = NewFromPool<FilterCacheItem>(*item_pool, *item_pool,
                                             info,
                                             status, headers, size,
                                             rubber, rubber_id,
                                             expires);

    cache.Put(info.key, *item);
}

static std::chrono::system_clock::time_point
parse_translate_time(const char *p, std::chrono::system_clock::duration offset)
{
    if (p == nullptr)
        return std::chrono::system_clock::from_time_t(-1);

    auto t = http_date_parse(p);
    if (t != std::chrono::system_clock::from_time_t(-1))
        t += offset;

    return t;
}

/** check whether the HTTP response should be put into the cache */
static bool
filter_cache_response_evaluate(FilterCacheInfo &info,
                               http_status_t status, const StringMap &headers,
                               off_t body_available)
{
    const char *p;

    if (status != HTTP_STATUS_OK)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    p = headers.Get("cache-control");
    if (p != nullptr && http_list_contains(p, "no-store"))
        return false;

    const auto now = std::chrono::system_clock::now();
    std::chrono::system_clock::duration offset = std::chrono::system_clock::duration::zero();

    p = headers.Get("date");
    if (p != nullptr) {
        auto date = http_date_parse(p);
        if (date != std::chrono::system_clock::from_time_t(-1))
            offset = now - date;
    }

    if (info.expires == std::chrono::system_clock::from_time_t(-1)) {
        info.expires = parse_translate_time(headers.Get("expires"), offset);
        if (info.expires != std::chrono::system_clock::from_time_t(-1) &&
            info.expires < now)
            LogConcat(2, "FilterCache", "invalid 'expires' header");
    }

    /*
    info.out_etag = headers.Get("etag");
    */

    return true;
}

inline void
FilterCacheRequest::OnTimeout()
{
    /* reading the response has taken too long already; don't store
       this resource */
    LogConcat(4, "FilterCache", "timeout ", info.key);
    filter_cache_request_abort(this);
}

/*
 * RubberSinkHandler
 *
 */

void
FilterCacheRequest::RubberDone(unsigned rubber_id, size_t size)
{
    response.cancel_ptr = nullptr;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    cache.Put(info, response.status, *response.headers, rubber_id, size);

    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberOutOfMemory()
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "nocache oom ", info.key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberTooLarge()
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "nocache too large %s\n", info.key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberError(std::exception_ptr ep)
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "body_abort ", info.key, ": ", ep);
    filter_cache_request_release(this);
}

/*
 * http response handler
 *
 */

void
FilterCacheRequest::OnHttpResponse(http_status_t status, StringMap &&headers,
                                   Istream *body) noexcept
{
    auto &_caller_pool = caller_pool;

    off_t available = body == nullptr ? 0 : body->GetAvailable(true);

    if (!filter_cache_response_evaluate(info,
                                        status, headers, available)) {
        /* don't cache response */
        LogConcat(4, "FilterCache", "nocache ", info.key);

        handler.InvokeResponse(status, std::move(headers), body);
        pool_unref(&_caller_pool);
        return;
    }

    if (body == nullptr) {
        response.cancel_ptr = nullptr;

        cache.Put(info, status, headers, 0, 0);
    } else {
        pool_ref(&pool);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(pool, *body,
                               cache.event_loop,
                               false, false);

        response.status = status;
        response.headers = strmap_dup(&pool, &headers);

        cache.requests.push_front(*this);

        timeout_event.Add(fcache_request_timeout);

        sink_rubber_new(pool, istream_tee_second(*body),
                        cache.rubber, cacheable_size_limit,
                        *this,
                        response.cancel_ptr);

        /* just in case our handler closes the body without looking at
           it: defer an Istream::Read() call for the Rubber sink */
        istream_tee_defer_read(*body);
    }

    handler.InvokeResponse(status, std::move(headers), body);
    pool_unref(&_caller_pool);
}

void
FilterCacheRequest::OnHttpError(std::exception_ptr ep) noexcept
{
    ep = NestException(ep, FormatRuntimeError("fcache %s", info.key));

    auto &_caller_pool = caller_pool;
    handler.InvokeError(ep);
    pool_unref(&_caller_pool);
}

/*
 * constructor and public methods
 *
 */

FilterCache::FilterCache(struct pool &_pool, size_t max_size,
                         EventLoop &_event_loop,
                         ResourceLoader &_resource_loader)
    :pool(*pool_new_libc(&_pool, "filter_cache")),
     /* leave 12.5% of the rubber allocator empty, to increase the
        chances that a hole can be found for a new allocation, to
        reduce the pressure that rubber_compress() creates */
     cache(_event_loop, 65521, max_size * 7 / 8),
     rubber(max_size),
     slice_pool(slice_pool_new(1024, 65536)),
     compress_timer(_event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     event_loop(_event_loop),
     resource_loader(_resource_loader) {
    compress_timer.Add(fcache_compress_interval);
}

FilterCache *
filter_cache_new(struct pool *pool, size_t max_size,
                 EventLoop &event_loop,
                 ResourceLoader &resource_loader)
{
    assert(max_size > 0);

    return new FilterCache(*pool, max_size,
                           event_loop, resource_loader);
}

inline FilterCache::~FilterCache()
{
    requests.clear_and_dispose(filter_cache_request_abort);

    compress_timer.Cancel();

    cache.Flush();
    slice_pool_free(slice_pool);

    pool_unref(&pool);
}

void
filter_cache_close(FilterCache *cache)
{
    delete cache;
}

void
filter_cache_fork_cow(FilterCache &cache, bool inherit)
{
    cache.ForkCow(inherit);
}

AllocatorStats
filter_cache_get_stats(const FilterCache &cache)
{
    return cache.GetStats();
}

void
filter_cache_flush(FilterCache &cache)
{
    cache.Flush();
}

void
FilterCache::Miss(struct pool &caller_pool,
                  FilterCacheInfo &&info,
                  const ResourceAddress &address,
                  http_status_t status, StringMap &&headers,
                  UnusedIstreamPtr body, const char *body_etag,
                  HttpResponseHandler &_handler,
                  CancellablePointer &cancel_ptr)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    auto *request_pool = pool_new_linear(&pool, "filter_cache_request", 8192);

    auto request = NewFromPool<FilterCacheRequest>(*request_pool, *request_pool, caller_pool,
                                                   *this, _handler, info);

    LogConcat(4, "FilterCache", "miss ", info.key);

    pool_ref(&caller_pool);
    resource_loader.SendRequest(*request_pool, 0,
                                HTTP_METHOD_POST, address,
                                status, std::move(headers),
                                std::move(body), body_etag,
                                *request,
                                async_unref_on_abort(caller_pool, cancel_ptr));
    pool_unref(request_pool);
}

void
FilterCache::Serve(FilterCacheItem &item,
                   struct pool &caller_pool,
                   HttpResponseHandler &handler)
{
    LogConcat(4, "FilterCache", "serve ", item.info.key);

    /* XXX hold reference on item */

    assert(item.rubber_id == 0 || ((CacheItem &)item).size >= item.size);

    Istream *response_body = item.rubber_id != 0
        ? istream_rubber_new(caller_pool, rubber, item.rubber_id,
                             0, item.size, false)
        : istream_null_new(&caller_pool);

    response_body = istream_unlock_new(caller_pool, *response_body, item);

    handler.InvokeResponse(item.status,
                           StringMap(ShallowCopy(), caller_pool, item.headers),
                           response_body);
}

void
FilterCache::Hit(FilterCacheItem &item,
                 struct pool &caller_pool,
                 HttpResponseHandler &handler)
{
    Serve(item, caller_pool, handler);
}

void
FilterCache::Get(struct pool &caller_pool,
                 const ResourceAddress &address,
                 const char *source_id,
                 http_status_t status, StringMap &&headers,
                 UnusedIstreamPtr body,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr)
{
    auto *info = filter_cache_request_evaluate(caller_pool, address,
                                               source_id, headers);
    if (info != nullptr) {
        FilterCacheItem *item
            = (FilterCacheItem *)cache.Get(info->key);

        if (item == nullptr)
            Miss(caller_pool, std::move(*info),
                 address, status, std::move(headers),
                 std::move(body), source_id,
                 handler, cancel_ptr);
        else {
            body.Clear();
            Hit(*item, caller_pool, handler);
        }
    } else {
        resource_loader.SendRequest(caller_pool, 0,
                                    HTTP_METHOD_POST, address,
                                    status, std::move(headers),
                                    std::move(body), source_id,
                                    handler, cancel_ptr);
    }
}

void
filter_cache_request(FilterCache &cache,
                     struct pool &pool,
                     const ResourceAddress &address,
                     const char *source_id,
                     http_status_t status, StringMap &&headers,
                     UnusedIstreamPtr body,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr)
{
    cache.Get(pool, address, source_id, status, std::move(headers),
              std::move(body), handler, cancel_ptr);
}
