/*
 * Copyright 2007-2019 Content Management AG
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
#include "http/HeaderWriter.hxx"
#include "strmap.hxx"
#include "HttpResponseHandler.hxx"
#include "AllocatorPtr.hxx"
#include "pool/tpool.hxx"
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
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "event/TimerEvent.hxx"
#include "event/Loop.hxx"
#include "io/Logger.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"
#include "util/LeakDetector.hxx"

#include <boost/intrusive/list.hpp>

#include <unordered_map>

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
static constexpr Event::Duration fcache_request_timeout = std::chrono::minutes(1);

static constexpr Event::Duration fcache_compress_interval = std::chrono::minutes(10);

/**
 * The default "expires" duration [s] if no expiration was given for
 * the input.
 */
static constexpr auto fcache_default_expires = std::chrono::hours(7 * 24);

struct FilterCacheInfo {
    /** when will the cached resource expire? (beng-proxy time) */
    std::chrono::system_clock::time_point expires =
        std::chrono::system_clock::from_time_t(-1);

    const char *tag;

    /** the final resource id */
    const char *key;

    FilterCacheInfo(const char *_tag, const char *_key) noexcept
        :tag(_tag), key(_key) {}

    FilterCacheInfo(struct pool &pool, const FilterCacheInfo &src) noexcept
        :expires(src.expires),
         tag(p_strdup_checked(&pool, src.tag)),
         key(p_strdup(&pool, src.key)) {}

    FilterCacheInfo(FilterCacheInfo &&src) = default;

    FilterCacheInfo &operator=(const FilterCacheInfo &) = delete;
};

struct FilterCacheItem final : PoolHolder, CacheItem, LeakDetector {
    using AutoUnlink =
        boost::intrusive::link_mode<boost::intrusive::auto_unlink>;
    using PerTagHook = boost::intrusive::list_member_hook<AutoUnlink>;

    /**
     * A doubly linked list of cache items with the same cache tag.
     */
    PerTagHook per_tag_siblings;

    const http_status_t status;
    StringMap headers;

    const size_t size;

    const RubberAllocation body;

    FilterCacheItem(PoolPtr &&_pool,
                    std::chrono::steady_clock::time_point now,
                    std::chrono::system_clock::time_point system_now,
                    http_status_t _status, const StringMap &_headers,
                    size_t _size, RubberAllocation &&_body,
                    std::chrono::system_clock::time_point _expires) noexcept
        :PoolHolder(std::move(_pool)),
         CacheItem(now, system_now, _expires, pool_netto_size(pool) + _size),
         status(_status), headers(pool, _headers),
         size(_size), body(std::move(_body)) {
    }

    using PoolHolder::GetPool;

    /* virtual methods from class CacheItem */
    void Destroy() noexcept override {
        pool_trash(pool);
        this->~FilterCacheItem();
    }

};

class FilterCacheRequest final
    : PoolHolder, HttpResponseHandler, RubberSinkHandler,
      Cancellable, LeakDetector {

public:
    static constexpr auto link_mode = boost::intrusive::auto_unlink;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

private:
    const PoolPtr caller_pool;
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

    CancellablePointer cancel_ptr;

public:
    FilterCacheRequest(PoolPtr &&_pool, struct pool &_caller_pool,
                       FilterCache &_cache,
                       HttpResponseHandler &_handler,
                       const FilterCacheInfo &_info) noexcept;

    void Start(ResourceLoader &resource_loader,
               const ResourceAddress &address,
               http_status_t status, StringMap &&headers,
               UnusedIstreamPtr body, const char *body_etag,
               CancellablePointer &caller_cancel_ptr) noexcept {
        caller_cancel_ptr = *this;
        resource_loader.SendRequest(pool, 0, nullptr, nullptr,
                                    HTTP_METHOD_POST, address,
                                    status, std::move(headers),
                                    std::move(body), body_etag,
                                    *this,
                                    cancel_ptr);
    }

    /**
     * Release resources held by this request.
     */
    void Destroy() noexcept;

    /**
     * Cancel storing the response body.
     */
    void CancelStore() noexcept;

private:
    void OnTimeout() noexcept;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(RubberAllocation &&a, size_t size) noexcept override;
    void RubberOutOfMemory() noexcept override;
    void RubberTooLarge() noexcept override;
    void RubberError(std::exception_ptr ep) noexcept override;
};

class FilterCache final : LeakDetector {
    friend class FilterCacheRequest;

    PoolPtr pool;
    SlicePool slice_pool;
    Rubber rubber;
    Cache cache;

    using PerTagHook =
        boost::intrusive::member_hook<FilterCacheItem,
                                      FilterCacheItem::PerTagHook,
                                      &FilterCacheItem::per_tag_siblings>;
    using PerTagList =
        boost::intrusive::list<FilterCacheItem, PerTagHook,
                               boost::intrusive::constant_time_size<false>>;

    /**
     * Lookup table to speed up FlushTag().
     */
    std::unordered_map<std::string, PerTagList> per_tag;

    TimerEvent compress_timer;

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

    ~FilterCache() noexcept;

    auto &GetEventLoop() const noexcept {
        return compress_timer.GetEventLoop();
    }

    void ForkCow(bool inherit) noexcept {
        rubber.ForkCow(inherit);
        slice_pool.ForkCow(inherit);
    }

    AllocatorStats GetStats() const noexcept {
        return slice_pool.GetStats() + rubber.GetStats();
    }

    void Flush() noexcept {
        cache.Flush();
        Compress();
    }

    void FlushTag(const char *tag) noexcept;

    void Get(struct pool &caller_pool,
             const char *cache_tag,
             const ResourceAddress &address,
             const char *source_id,
             http_status_t status, StringMap &&headers,
             UnusedIstreamPtr body,
             HttpResponseHandler &handler,
             CancellablePointer &cancel_ptr) noexcept;

    void Put(const FilterCacheInfo &info,
             http_status_t status, const StringMap &headers,
             RubberAllocation &&a, size_t size) noexcept;

private:
    void Miss(struct pool &caller_pool,
              FilterCacheInfo &&info,
              const ResourceAddress &address,
              http_status_t status, StringMap &&headers,
              UnusedIstreamPtr body, const char *body_etag,
              HttpResponseHandler &_handler,
              CancellablePointer &cancel_ptr) noexcept;

    void Serve(FilterCacheItem &item,
               struct pool &caller_pool,
               HttpResponseHandler &handler) noexcept;

    void Hit(FilterCacheItem &item,
             struct pool &caller_pool,
             HttpResponseHandler &handler) noexcept;

    void Compress() noexcept {
        rubber.Compress();
        slice_pool.Compress();
    }

    void OnCompressTimer() noexcept {
        Compress();
        compress_timer.Schedule(fcache_compress_interval);
    }
};

FilterCacheRequest::FilterCacheRequest(PoolPtr &&_pool,
                                       struct pool &_caller_pool,
                                       FilterCache &_cache,
                                       HttpResponseHandler &_handler,
                                       const FilterCacheInfo &_info) noexcept
    :PoolHolder(std::move(_pool)), caller_pool(_caller_pool),
     cache(_cache),
     handler(_handler),
     info(pool, _info),
     timeout_event(cache.GetEventLoop(), BIND_THIS_METHOD(OnTimeout))
{
}

void
FilterCacheRequest::Destroy() noexcept
{
    assert(!response.cancel_ptr);

    this->~FilterCacheRequest();
}

void
FilterCacheRequest::CancelStore() noexcept
{
    assert(response.cancel_ptr);

    response.cancel_ptr.CancelAndClear();
    Destroy();
}

/* check whether the request could produce a cacheable response */
static FilterCacheInfo *
filter_cache_request_evaluate(struct pool &pool,
                              const char *tag,
                              const ResourceAddress &address,
                              const char *source_id,
                              const StringMap &headers) noexcept
{
    if (source_id == nullptr)
        return nullptr;

    const char *user = headers.Get("x-cm4all-beng-user");
    if (user == nullptr)
        user = "";

    return NewFromPool<FilterCacheInfo>(pool, tag,
                                        p_strcat(&pool, source_id, "|",
                                                 user, "|",
                                                 address.GetId(AllocatorPtr(pool)), nullptr));
}

void
FilterCache::Put(const FilterCacheInfo &info,
                 http_status_t status, const StringMap &headers,
                 RubberAllocation &&a, size_t size) noexcept
{
    LogConcat(4, "FilterCache", "put ", info.key);

    std::chrono::system_clock::time_point expires;
    if (info.expires == std::chrono::system_clock::from_time_t(-1))
        expires = GetEventLoop().SystemNow() + fcache_default_expires;
    else
        expires = info.expires;

    auto item = NewFromPool<FilterCacheItem>(pool_new_slice(pool, "FilterCacheItem", &slice_pool),
                                             cache.SteadyNow(),
                                             cache.SystemNow(),
                                             status, headers, size,
                                             std::move(a),
                                             expires);

    if (info.tag != nullptr)
        per_tag[info.tag].push_back(*item);

    cache.Put(p_strdup(item->GetPool(), info.key), *item);
}

static std::chrono::system_clock::time_point
parse_translate_time(const char *p,
                     std::chrono::system_clock::duration offset) noexcept
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
filter_cache_response_evaluate(EventLoop &event_loop, FilterCacheInfo &info,
                               http_status_t status, const StringMap &headers,
                               off_t body_available) noexcept
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

    const auto now = event_loop.SystemNow();
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
FilterCacheRequest::OnTimeout() noexcept
{
    /* reading the response has taken too long already; don't store
       this resource */
    LogConcat(4, "FilterCache", "timeout ", info.key);
    CancelStore();
}

/*
 * RubberSinkHandler
 *
 */

void
FilterCacheRequest::RubberDone(RubberAllocation &&a, size_t size) noexcept
{
    response.cancel_ptr = nullptr;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    cache.Put(info, response.status, *response.headers, std::move(a), size);

    Destroy();
}

void
FilterCacheRequest::RubberOutOfMemory() noexcept
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "nocache oom ", info.key);
    Destroy();
}

void
FilterCacheRequest::RubberTooLarge() noexcept
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "nocache too large %s\n", info.key);
    Destroy();
}

void
FilterCacheRequest::RubberError(std::exception_ptr ep) noexcept
{
    response.cancel_ptr = nullptr;

    LogConcat(4, "FilterCache", "body_abort ", info.key, ": ", ep);
    Destroy();
}

void
FilterCacheRequest::Cancel() noexcept
{
    cancel_ptr.Cancel();
    Destroy();
}

/*
 * http response handler
 *
 */

void
FilterCacheRequest::OnHttpResponse(http_status_t status, StringMap &&headers,
                                   UnusedIstreamPtr body) noexcept
{
    /* make sure the caller pool gets unreferenced upon returning */
    const auto _caller_pool = std::move(caller_pool);

    off_t available = body ? body.GetAvailable(true) : 0;

    if (!filter_cache_response_evaluate(cache.GetEventLoop(), info,
                                        status, headers, available)) {
        /* don't cache response */
        LogConcat(4, "FilterCache", "nocache ", info.key);

        handler.InvokeResponse(status, std::move(headers), std::move(body));
        Destroy();
        return;
    }

    /* copy the HttpResponseHandler reference to the stack, because
       the sink_rubber_new() call may destroy this object */
    auto &_handler = handler;

    if (!body) {
        response.cancel_ptr = nullptr;

        cache.Put(info, status, headers, {}, 0);
    } else {
        /* tee the body: one goes to our client, and one goes into the
           cache */
        auto tee = istream_tee_new(pool, std::move(body),
                                   cache.GetEventLoop(),
                                   false,
                                   /* the second one must be weak
                                      because closing the first one
                                      may imply invalidating our input
                                      (because its pool is going to be
                                      trashed), triggering the pool
                                      leak detector */
                                   true,
                                   /* just in case our handler closes
                                      the body without looking at it:
                                      defer an Istream::Read() call
                                      for the Rubber sink */
                                   true);

        response.status = status;
        response.headers = strmap_dup(pool, &headers);

        cache.requests.push_front(*this);

        timeout_event.Schedule(fcache_request_timeout);

        sink_rubber_new(pool, std::move(tee.second),
                        cache.rubber, cacheable_size_limit,
                        *this,
                        response.cancel_ptr);

        body = std::move(tee.first);
    }

    _handler.InvokeResponse(status, std::move(headers), std::move(body));
}

void
FilterCacheRequest::OnHttpError(std::exception_ptr ep) noexcept
{
    ep = NestException(ep, FormatRuntimeError("fcache %s", info.key));

    handler.InvokeError(ep);
    Destroy();
}

/*
 * constructor and public methods
 *
 */

FilterCache::FilterCache(struct pool &_pool, size_t max_size,
                         EventLoop &_event_loop,
                         ResourceLoader &_resource_loader)
    :pool(pool_new_libc(&_pool, "filter_cache")),
     slice_pool(1024, 65536),
     rubber(max_size),
     /* leave 12.5% of the rubber allocator empty, to increase the
        chances that a hole can be found for a new allocation, to
        reduce the pressure that rubber_compress() creates */
     cache(_event_loop, 65521, max_size * 7 / 8),
     compress_timer(_event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     resource_loader(_resource_loader) {
    compress_timer.Schedule(fcache_compress_interval);
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

inline FilterCache::~FilterCache() noexcept
{
    requests.clear_and_dispose([](FilterCacheRequest *r){ r->CancelStore(); });
}

void
filter_cache_close(FilterCache *cache) noexcept
{
    delete cache;
}

void
filter_cache_fork_cow(FilterCache &cache, bool inherit) noexcept
{
    cache.ForkCow(inherit);
}

AllocatorStats
filter_cache_get_stats(const FilterCache &cache) noexcept
{
    return cache.GetStats();
}

void
filter_cache_flush(FilterCache &cache) noexcept
{
    cache.Flush();
}

void
FilterCache::FlushTag(const char *tag) noexcept
{
    auto i = per_tag.find(tag);
    if (i == per_tag.end())
        return;

    auto &list = i->second;
    while (!list.empty())
        cache.Remove(list.front());
}

void
filter_cache_flush_tag(FilterCache &cache, const char *tag) noexcept
{
    cache.FlushTag(tag);
}

void
FilterCache::Miss(struct pool &caller_pool,
                  FilterCacheInfo &&info,
                  const ResourceAddress &address,
                  http_status_t status, StringMap &&headers,
                  UnusedIstreamPtr body, const char *body_etag,
                  HttpResponseHandler &_handler,
                  CancellablePointer &cancel_ptr) noexcept
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    auto request_pool = pool_new_linear(pool, "filter_cache_request", 8192);

    auto request = NewFromPool<FilterCacheRequest>(std::move(request_pool),
                                                   caller_pool,
                                                   *this, _handler, info);

    LogConcat(4, "FilterCache", "miss ", info.key);

    request->Start(resource_loader, address,
                   status, std::move(headers),
                   std::move(body), body_etag,
                   cancel_ptr);
}

void
FilterCache::Serve(FilterCacheItem &item,
                   struct pool &caller_pool,
                   HttpResponseHandler &handler) noexcept
{
    LogConcat(4, "FilterCache", "serve ", item.GetKey());

    /* XXX hold reference on item */

    assert(!item.body || ((CacheItem &)item).GetSize() >= item.size);

    auto response_body = item.body
        ? istream_rubber_new(caller_pool, rubber, item.body.GetId(),
                             0, item.size, false)
        : istream_null_new(caller_pool);

    response_body = istream_unlock_new(caller_pool, std::move(response_body), item);

    handler.InvokeResponse(item.status,
                           StringMap(ShallowCopy(), caller_pool, item.headers),
                           std::move(response_body));
}

void
FilterCache::Hit(FilterCacheItem &item,
                 struct pool &caller_pool,
                 HttpResponseHandler &handler) noexcept
{
    Serve(item, caller_pool, handler);
}

void
FilterCache::Get(struct pool &caller_pool,
                 const char *cache_tag,
                 const ResourceAddress &address,
                 const char *source_id,
                 http_status_t status, StringMap &&headers,
                 UnusedIstreamPtr body,
                 HttpResponseHandler &handler,
                 CancellablePointer &cancel_ptr) noexcept
{
    auto *info = filter_cache_request_evaluate(caller_pool, cache_tag, address,
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
        resource_loader.SendRequest(caller_pool, 0, cache_tag, nullptr,
                                    HTTP_METHOD_POST, address,
                                    status, std::move(headers),
                                    std::move(body), source_id,
                                    handler, cancel_ptr);
    }
}

void
filter_cache_request(FilterCache &cache,
                     struct pool &pool,
                     const char *cache_tag,
                     const ResourceAddress &address,
                     const char *source_id,
                     http_status_t status, StringMap &&headers,
                     UnusedIstreamPtr body,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) noexcept
{
    cache.Get(pool, cache_tag, address, source_id,
              status, std::move(headers),
              std::move(body), handler, cancel_ptr);
}
