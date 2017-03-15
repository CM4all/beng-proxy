/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcache.hxx"
#include "cache.hxx"
#include "http_request.hxx"
#include "header_writer.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "http_date.hxx"
#include "abort_unref.hxx"
#include "tpool.hxx"
#include "http_util.hxx"
#include "ResourceAddress.hxx"
#include "ResourceLoader.hxx"
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
#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>

#include <glib.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

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
            rubber_remove(&rubber, rubber_id);

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
                        Istream *body) override;
    void OnHttpError(GError *error) override;

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(GError *error) override;
};

class FilterCache {
public:
    struct pool &pool;
    Cache cache;
    Rubber *const rubber;
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

    FilterCache(struct pool &_pool, size_t max_size,
                EventLoop &_event_loop, ResourceLoader &_resource_loader);

    ~FilterCache();

private:
    void OnCompressTimer() {
        rubber_compress(rubber);
        slice_pool_compress(slice_pool);
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

    /* DeleteUnrefTrashPool() poisons the object and trashes the pool,
       which breaks the istream_read() call in
       filter_cache_response_response() and causes an assertion
       failure when the sink_rubber closes the stream */
    //DeleteUnrefTrashPool(*request->pool, request);
    request->~FilterCacheRequest();
    pool_unref(&request->pool);
    /* TODO: eliminate the above workaround */
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

static void
filter_cache_put(FilterCacheRequest *request,
                 unsigned rubber_id, size_t size)
{
    assert(request != nullptr);

    cache_log(4, "filter_cache: put %s\n", request->info.key);

    std::chrono::system_clock::time_point expires;
    if (request->info.expires == std::chrono::system_clock::from_time_t(-1))
        expires = std::chrono::system_clock::now() + fcache_default_expires;
    else
        expires = request->info.expires;

    struct pool *pool = pool_new_slice(&request->cache.pool, "FilterCacheItem",
                                       request->cache.slice_pool);
    auto item = NewFromPool<FilterCacheItem>(*pool, *pool,
                                             request->info,
                                             request->response.status,
                                             *request->response.headers,
                                             size,
                                             *request->cache.rubber,
                                             rubber_id,
                                             expires);

    request->cache.cache.Put(item->info.key, *item);
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
            cache_log(2, "invalid 'expires' header\n");
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
    cache_log(4, "filter_cache: timeout %s\n", info.key);
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
    filter_cache_put(this, rubber_id, size);

    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberOutOfMemory()
{
    response.cancel_ptr = nullptr;

    cache_log(4, "filter_cache: nocache oom %s\n", info.key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberTooLarge()
{
    response.cancel_ptr = nullptr;

    cache_log(4, "filter_cache: nocache too large %s\n", info.key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberError(GError *error)
{
    response.cancel_ptr = nullptr;

    cache_log(4, "filter_cache: body_abort %s: %s\n",
              info.key, error->message);
    g_error_free(error);

    filter_cache_request_release(this);
}

/*
 * http response handler
 *
 */

void
FilterCacheRequest::OnHttpResponse(http_status_t status, StringMap &&headers,
                                   Istream *body)
{
    auto &_caller_pool = caller_pool;

    off_t available = body == nullptr ? 0 : body->GetAvailable(true);

    if (!filter_cache_response_evaluate(info,
                                        status, headers, available)) {
        /* don't cache response */
        cache_log(4, "filter_cache: nocache %s\n", info.key);

        handler.InvokeResponse(status, std::move(headers), body);
        pool_unref(&_caller_pool);
        return;
    }

    if (body == nullptr) {
        response.cancel_ptr = nullptr;

        response.status = status;
        response.headers = &headers;

        filter_cache_put(this, 0, 0);
    } else {
        pool_ref(&pool);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(pool, *body,
                               cache.event_loop,
                               false, false);

        response.status = status;
        response.headers = strmap_dup(&pool, &headers);

        pool_ref(&pool);

        cache.requests.push_front(*this);

        timeout_event.Add(fcache_request_timeout);

        sink_rubber_new(pool, istream_tee_second(*body),
                        *cache.rubber, cacheable_size_limit,
                        *this,
                        response.cancel_ptr);
    }

    handler.InvokeResponse(status, std::move(headers), body);
    pool_unref(&caller_pool);

    if (body != nullptr) {
        if (response.cancel_ptr)
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_tee_second(*body).Read();

        pool_unref(&pool);
    }
}

void
FilterCacheRequest::OnHttpError(GError *error)
{
    g_prefix_error(&error, "http_cache %s: ", info.key);

    auto &_caller_pool = caller_pool;
    handler.InvokeError(error);
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
     rubber(rubber_new(max_size)),
     slice_pool(slice_pool_new(1024, 65536)),
     compress_timer(_event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     event_loop(_event_loop),
     resource_loader(_resource_loader) {
    if (rubber == nullptr) {
        fprintf(stderr, "Failed to allocate filter cache\n");
        _exit(2);
    }

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
    rubber_free(rubber);

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
    rubber_fork_cow(cache.rubber, inherit);
    slice_pool_fork_cow(*cache.slice_pool, inherit);
}

AllocatorStats
filter_cache_get_stats(const FilterCache &cache)
{
    return slice_pool_get_stats(*cache.slice_pool)
            + rubber_get_stats(*cache.rubber);
}

void
filter_cache_flush(FilterCache &cache)
{
    cache.cache.Flush();
    rubber_compress(cache.rubber);
    slice_pool_compress(cache.slice_pool);
}

static void
filter_cache_miss(FilterCache &cache, struct pool &caller_pool,
                  FilterCacheInfo &&info,
                  const ResourceAddress &address,
                  http_status_t status, StringMap &&headers,
                  Istream *body, const char *body_etag,
                  HttpResponseHandler &_handler,
                  CancellablePointer &cancel_ptr)
{
    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    auto *pool = pool_new_linear(&cache.pool, "filter_cache_request", 8192);

    auto request = NewFromPool<FilterCacheRequest>(*pool, *pool, caller_pool,
                                                   cache, _handler, info);

    cache_log(4, "filter_cache: miss %s\n", info.key);

    pool_ref(&caller_pool);
    cache.resource_loader.SendRequest(*pool, 0,
                                      HTTP_METHOD_POST, address,
                                      status, std::move(headers),
                                      body, body_etag,
                                      *request,
                                      async_unref_on_abort(caller_pool, cancel_ptr));
    pool_unref(pool);
}

static void
filter_cache_serve(FilterCache &cache, FilterCacheItem &item,
                   struct pool &pool, Istream *body,
                   HttpResponseHandler &handler)
{
    if (body != nullptr)
        body->CloseUnused();

    cache_log(4, "filter_cache: serve %s\n", item.info.key);

    /* XXX hold reference on item */

    assert(item.rubber_id == 0 || ((CacheItem &)item).size >= item.size);

    Istream *response_body = item.rubber_id != 0
        ? istream_rubber_new(pool, *cache.rubber, item.rubber_id,
                             0, item.size, false)
        : istream_null_new(&pool);

    response_body = istream_unlock_new(pool, *response_body, item);

    handler.InvokeResponse(item.status,
                           StringMap(ShallowCopy(), pool, item.headers),
                           response_body);
}

static void
filter_cache_found(FilterCache &cache,
                   FilterCacheItem &item,
                   struct pool &pool, Istream *body,
                   HttpResponseHandler &handler)
{
    filter_cache_serve(cache, item, pool, body, handler);
}

void
filter_cache_request(FilterCache &cache,
                     struct pool &pool,
                     const ResourceAddress &address,
                     const char *source_id,
                     http_status_t status, StringMap &&headers,
                     Istream *body,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr)
{
    auto *info = filter_cache_request_evaluate(pool, address,
                                               source_id, headers);
    if (info != nullptr) {
        FilterCacheItem *item
            = (FilterCacheItem *)cache.cache.Get(info->key);

        if (item == nullptr)
            filter_cache_miss(cache, pool, std::move(*info),
                              address, status, std::move(headers),
                              body, source_id,
                              handler, cancel_ptr);
        else
            filter_cache_found(cache, *item, pool, body, handler);
    } else {
        cache.resource_loader.SendRequest(pool, 0,
                                          HTTP_METHOD_POST, address,
                                          status, std::move(headers),
                                          body, source_id,
                                          handler, cancel_ptr);
    }
}
