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
#include "date.h"
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
#include "async.hxx"
#include "pool.hxx"
#include "event/TimerEvent.hxx"

#include <boost/intrusive/list.hpp>

#include <glib.h>

#include <string.h>
#include <time.h>
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
    time_t expires;

    /** the final resource id */
    const char *key;

    FilterCacheInfo(const char *_key)
        :expires(-1), key(_key) {}

    FilterCacheInfo(struct pool &pool, const FilterCacheInfo &src)
        :expires(src.expires),
         key(p_strdup(&pool, src.key)) {}

    FilterCacheInfo(const FilterCacheInfo &) = delete;
};

struct FilterCacheItem {
    CacheItem item;

    struct pool &pool;

    const FilterCacheInfo info;

    const http_status_t status;
    struct strmap *const headers;

    const size_t size;
    Rubber &rubber;
    const unsigned rubber_id;

    FilterCacheItem(struct pool &_pool, const FilterCacheInfo &_info,
                    http_status_t _status, struct strmap *_headers,
                    size_t _size, Rubber &_rubber, unsigned _rubber_id,
                    std::chrono::system_clock::time_point _expires)
        :pool(_pool), info(pool, _info),
         status(_status), headers(_headers),
         size(_size), rubber(_rubber), rubber_id(_rubber_id) {
        item.Init(_expires, pool_netto_size(&pool) + size);
    }
};

struct FilterCacheRequest final : RubberSinkHandler {
    static constexpr auto link_mode = boost::intrusive::auto_unlink;
    typedef boost::intrusive::link_mode<link_mode> LinkMode;
    typedef boost::intrusive::list_member_hook<LinkMode> SiblingsHook;
    SiblingsHook siblings;

    struct pool *const pool, *const caller_pool;
    FilterCache *const cache;
    struct http_response_handler_ref handler;

    FilterCacheInfo *const info;

    struct {
        http_status_t status;
        struct strmap *headers;

        /**
         * A handle to abort the sink_rubber that copies response body
         * data into a new rubber allocation.
         */
        struct async_operation_ref async_ref;
    } response;

    /**
     * This event is initialized by the response callback, and limits
     * the duration for receiving the response body.
     */
    TimerEvent timeout_event;

    FilterCacheRequest(struct pool &_pool, struct pool &_caller_pool,
                       EventLoop &event_loop,
                       FilterCache &_cache,
                       FilterCacheInfo &_info)
        :pool(&_pool), caller_pool(&_caller_pool),
         cache(&_cache),
         info(&_info),
         timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)) {}

    void OnTimeout();

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(GError *error) override;
};

class FilterCache {
public:
    struct pool &pool;
    Cache *const cache;
    Rubber *rubber;
    SlicePool *slice_pool;

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

/**
 * Release resources held by this request.
 */
static void
filter_cache_request_release(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(!request->response.async_ref.IsDefined());

    request->timeout_event.Cancel();

    /* DeleteUnrefTrashPool() poisons the object and trashes the pool,
       which breaks the istream_read() call in
       filter_cache_response_response() and causes an assertion
       failure when the sink_rubber closes the stream */
    //DeleteUnrefTrashPool(*request->pool, request);
    request->~FilterCacheRequest();
    pool_unref(request->pool);
    /* TODO: eliminate the above workaround */
}

/**
 * Abort the request.
 */
static void
filter_cache_request_abort(struct FilterCacheRequest *request)
{
    assert(request != nullptr);
    assert(request->response.async_ref.IsDefined());

    request->response.async_ref.Abort();
    request->response.async_ref.Clear();
    filter_cache_request_release(request);
}

/* check whether the request could produce a cacheable response */
static FilterCacheInfo *
filter_cache_request_evaluate(struct pool &pool,
                              const ResourceAddress *address,
                              const char *source_id)
{
    if (source_id == nullptr)
        return nullptr;

    return NewFromPool<FilterCacheInfo>(pool,
                                        p_strcat(&pool, source_id, "|",
                                                 address->GetId(pool), nullptr));
}

static FilterCacheInfo *
filter_cache_info_dup(struct pool &pool, const FilterCacheInfo &src)
{
    return NewFromPool<FilterCacheInfo>(pool, pool, src);
}

static FilterCacheRequest *
filter_cache_request_dup(struct pool &pool, const FilterCacheRequest &src)
{
    auto dest = NewFromPool<FilterCacheRequest>(pool, pool, *src.caller_pool,
                                                src.cache->event_loop,
                                                *src.cache,
                                                *filter_cache_info_dup(pool, *src.info));
    dest->handler = src.handler;
    return dest;
}

static void
filter_cache_put(FilterCacheRequest *request,
                 unsigned rubber_id, size_t size)
{
    assert(request != nullptr);

    cache_log(4, "filter_cache: put %s\n", request->info->key);

    std::chrono::system_clock::time_point expires;
    if (request->info->expires == (time_t)-1)
        expires = std::chrono::system_clock::now() + fcache_default_expires;
    else
        expires = std::chrono::system_clock::from_time_t(request->info->expires);

    struct pool *pool = pool_new_slice(&request->cache->pool, "FilterCacheItem",
                                       request->cache->slice_pool);
    auto item = NewFromPool<FilterCacheItem>(*pool, *pool,
                                             *request->info,
                                             request->response.status,
                                             strmap_dup(pool,
                                                        request->response.headers),
                                             size,
                                             *request->cache->rubber,
                                             rubber_id,
                                             expires);

    cache_put(request->cache->cache,
              item->info.key, &item->item);
}

static time_t
parse_translate_time(const char *p, time_t offset)
{
    time_t t;

    if (p == nullptr)
        return (time_t)-1;

    t = http_date_parse(p);
    if (t != (time_t)-1)
        t += offset;

    return t;
}

/** check whether the HTTP response should be put into the cache */
static bool
filter_cache_response_evaluate(FilterCacheInfo *info,
                               http_status_t status, struct strmap *headers,
                               off_t body_available)
{
    time_t now, offset = 0;
    const char *p;

    if (status != HTTP_STATUS_OK)
        return false;

    if (body_available != (off_t)-1 && body_available > cacheable_size_limit)
        /* too large for the cache */
        return false;

    p = headers->Get("cache-control");
    if (p != nullptr && http_list_contains(p, "no-store"))
        return false;

    now = time(nullptr);

    p = headers->Get("date");
    if (p != nullptr) {
        time_t date = http_date_parse(p);
        if (date != (time_t)-1)
            offset = now - date;
    }

    if (info->expires == (time_t)-1) {
        info->expires = parse_translate_time(headers->Get("expires"), offset);
        if (info->expires != (time_t)-1 && info->expires < now)
            cache_log(2, "invalid 'expires' header\n");
    }

    /*
    info->out_etag = headers->Get("etag");
    */

    return true;
}

inline void
FilterCacheRequest::OnTimeout()
{
    /* reading the response has taken too long already; don't store
       this resource */
    cache_log(4, "filter_cache: timeout %s\n", info->key);
    filter_cache_request_abort(this);
}

/*
 * RubberSinkHandler
 *
 */

void
FilterCacheRequest::RubberDone(unsigned rubber_id, size_t size)
{
    response.async_ref.Clear();

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    filter_cache_put(this, rubber_id, size);

    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberOutOfMemory()
{
    response.async_ref.Clear();

    cache_log(4, "filter_cache: nocache oom %s\n", info->key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberTooLarge()
{
    response.async_ref.Clear();

    cache_log(4, "filter_cache: nocache too large %s\n", info->key);
    filter_cache_request_release(this);
}

void
FilterCacheRequest::RubberError(GError *error)
{
    response.async_ref.Clear();

    cache_log(4, "filter_cache: body_abort %s: %s\n",
              info->key, error->message);
    g_error_free(error);

    filter_cache_request_release(this);
}

/*
 * http response handler
 *
 */

static void
filter_cache_response_response(http_status_t status, struct strmap *headers,
                               Istream *body,
                               void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;
    off_t available;
    struct pool *caller_pool = request->caller_pool;

    available = body == nullptr ? 0 : body->GetAvailable(true);

    if (!filter_cache_response_evaluate(request->info,
                                        status, headers, available)) {
        /* don't cache response */
        cache_log(4, "filter_cache: nocache %s\n", request->info->key);

        request->handler.InvokeResponse(status, headers, body);
        pool_unref(caller_pool);
        return;
    }

    if (body == nullptr) {
        request->response.async_ref.Clear();

        request->response.status = status;
        request->response.headers = headers;

        filter_cache_put(request, 0, 0);
    } else {
        struct pool *pool;

        /* move all this stuff to a new pool, so istream_tee's second
           head can continue to fill the cache even if our caller gave
           up on it */
        pool = pool_new_linear(&request->cache->pool, "filter_cache_tee", 1024);
        request = filter_cache_request_dup(*pool, *request);

        /* tee the body: one goes to our client, and one goes into the
           cache */
        body = istream_tee_new(*request->pool, *body,
                               request->cache->event_loop,
                               false, true);

        request->response.status = status;
        request->response.headers = strmap_dup(request->pool, headers);

        pool_ref(request->pool);

        request->cache->requests.push_front(*request);

        request->timeout_event.Add(fcache_request_timeout);

        sink_rubber_new(*pool, istream_tee_second(*body),
                        *request->cache->rubber, cacheable_size_limit,
                        *request,
                        request->response.async_ref);
    }

    request->handler.InvokeResponse(status, headers, body);
    pool_unref(caller_pool);

    if (body != nullptr) {
        if (request->response.async_ref.IsDefined())
            /* just in case our handler has closed the body without
               looking at it: call istream_read() to start reading */
            istream_tee_second(*body).Read();

        pool_unref(request->pool);
    }
}

static void
filter_cache_response_abort(GError *error, void *ctx)
{
    FilterCacheRequest *request = (FilterCacheRequest *)ctx;

    g_prefix_error(&error, "http_cache %s: ", request->info->key);

    request->handler.InvokeAbort(error);
    pool_unref(request->caller_pool);
}

static const struct http_response_handler filter_cache_response_handler = {
    .response = filter_cache_response_response,
    .abort = filter_cache_response_abort,
};


/*
 * cache_class
 *
 */

static bool
filter_cache_item_validate(CacheItem *_item)
{
    FilterCacheItem *item = (FilterCacheItem *)_item;

    (void)item;
    return true;
}

static void
filter_cache_item_destroy(CacheItem *_item)
{
    FilterCacheItem *item = (FilterCacheItem *)_item;

    if (item->rubber_id != 0)
        rubber_remove(&item->rubber, item->rubber_id);

    DeleteUnrefTrashPool(item->pool, item);
}

static constexpr CacheClass filter_cache_class = {
    .validate = filter_cache_item_validate,
    .destroy = filter_cache_item_destroy,
};


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
     cache(cache_new(pool, _event_loop, filter_cache_class, 65521,
                     max_size * 7 / 8)),
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

    cache_close(cache);

    compress_timer.Cancel();

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
filter_cache_fork_cow(FilterCache *cache, bool inherit)
{
    rubber_fork_cow(cache->rubber, inherit);
    slice_pool_fork_cow(*cache->slice_pool, inherit);
}

AllocatorStats
filter_cache_get_stats(const FilterCache &cache)
{
    return slice_pool_get_stats(*cache.slice_pool)
            + rubber_get_stats(*cache.rubber);
}

void
filter_cache_flush(FilterCache *cache)
{
    cache_flush(cache->cache);
    rubber_compress(cache->rubber);
    slice_pool_compress(cache->slice_pool);
}

static void
filter_cache_miss(FilterCache &cache, struct pool &caller_pool,
                  FilterCacheInfo &info,
                  const ResourceAddress *address,
                  http_status_t status, struct strmap *headers,
                  Istream *body, const char *body_etag,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    struct pool *pool;

    /* the cache request may live longer than the caller pool, so
       allocate a new pool for it from cache->pool */
    pool = pool_new_linear(&cache.pool, "filter_cache_request", 8192);

    auto request = NewFromPool<FilterCacheRequest>(*pool, *pool, caller_pool,
                                                   cache.event_loop,
                                                   cache, info);
    request->handler.Set(*handler, handler_ctx);

    cache_log(4, "filter_cache: miss %s\n", info.key);

    pool_ref(&caller_pool);
    cache.resource_loader.SendRequest(*pool, 0,
                                      HTTP_METHOD_POST, *address,
                                      status, headers, body, body_etag,
                                      filter_cache_response_handler, request,
                                      async_unref_on_abort(caller_pool, *async_ref));
    pool_unref(pool);
}

static void
filter_cache_serve(FilterCache *cache, FilterCacheItem *item,
                   struct pool *pool, Istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx)
{
    struct http_response_handler_ref handler_ref;

    if (body != nullptr)
        body->CloseUnused();

    cache_log(4, "filter_cache: serve %s\n", item->info.key);

    handler_ref.Set(*handler, handler_ctx);

    /* XXX hold reference on item */

    assert(item->rubber_id == 0 || item->item.size >= item->size);

    Istream *response_body = item->rubber_id != 0
        ? istream_rubber_new(*pool, *cache->rubber, item->rubber_id,
                             0, item->size, false)
        : istream_null_new(pool);

    response_body = istream_unlock_new(*pool, *response_body,
                                       *cache->cache, item->item);

    handler_ref.InvokeResponse(item->status, item->headers, response_body);
}

static void
filter_cache_found(FilterCache *cache,
                   FilterCacheItem *item,
                   struct pool *pool, Istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx)
{
    filter_cache_serve(cache, item, pool, body, handler, handler_ctx);
}

void
filter_cache_request(FilterCache *cache,
                     struct pool *pool,
                     const ResourceAddress *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers,
                     Istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref)
{
    auto *info = filter_cache_request_evaluate(*pool, address, source_id);
    if (info != nullptr) {
        FilterCacheItem *item
            = (FilterCacheItem *)cache_get(cache->cache, info->key);

        if (item == nullptr)
            filter_cache_miss(*cache, *pool, *info,
                              address, status, headers, body, source_id,
                              handler, handler_ctx, async_ref);
        else
            filter_cache_found(cache, item, pool, body,
                               handler, handler_ctx);
    } else {
        cache->resource_loader.SendRequest(*pool, 0,
                                           HTTP_METHOD_POST, *address,
                                           status, headers, body, source_id,
                                           *handler, handler_ctx, *async_ref);
    }
}
