/*
 * A cache for NFS files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_cache.hxx"
#include "nfs_stock.hxx"
#include "nfs_client.hxx"
#include "istream_nfs.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "istream_unlock.hxx"
#include "istream_rubber.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_tee.hxx"
#include "AllocatorStats.hxx"
#include "cache.hxx"
#include "async.hxx"
#include "event/TimerEvent.hxx"
#include "event/Callback.hxx"

#include <inline/list.h>

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef CACHE_LOG
#include <daemon/log.h>
#define cache_log(...) daemon_log(__VA_ARGS__)
#else
#define cache_log(...) do {} while (0)
#endif

static constexpr struct timeval nfs_cache_compress_interval = { 600, 0 };

struct NfsCacheItem;

struct NfsCache {
    struct pool &pool;

    NfsStock &stock;
    EventLoop &event_loop;

    struct cache &cache;

    TimerEvent compress_timer;

    Rubber &rubber;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    struct list_head requests;

    NfsCache(struct pool &_pool, size_t max_size, NfsStock &_stock,
             EventLoop &_event_loop);

    ~NfsCache() {
        cache_close(&cache);
        compress_timer.Deinit();
        rubber_free(&rubber);
        pool_unref(&pool);
    }

private:
    void OnCompressTimer() {
        rubber_compress(&rubber);
        compress_timer.Add(nfs_cache_compress_interval);
    }
};

struct NfsCacheRequest {
    struct pool &pool;

    NfsCache &cache;

    const char *key;
    const char *path;

    const NfsCacheHandler &handler;
    void *handler_ctx;
    struct async_operation_ref &async_ref;

    NfsCacheRequest(struct pool &_pool, NfsCache &_cache,
                    const char *_key, const char *_path,
                    const NfsCacheHandler &_handler, void *_ctx,
                    struct async_operation_ref &_async_ref)
        :pool(_pool), cache(_cache),
         key(_key), path(_path),
         handler(_handler), handler_ctx(_ctx),
         async_ref(_async_ref) {}
};

struct NfsCacheHandle {
    NfsCache &cache;
    const char *key;

    NfsFileHandle *file;
    NfsCacheItem *item;
    const struct stat &stat;
};

struct NfsCacheStore final : RubberSinkHandler {
    struct list_head siblings;

    struct pool &pool;

    NfsCache &cache;

    const char *key;

    struct stat stat;

    TimerEvent timeout_event;
    struct async_operation_ref async_ref;

    NfsCacheStore(struct pool &_pool, NfsCache &_cache,
                  const char *_key, const struct stat &_st)
        :pool(_pool), cache(_cache),
         key(_key),
         stat(_st),
         timeout_event(MakeSimpleEventCallback(NfsCacheStore, OnTimeout),
                       this) {}

    /**
     * Release resources held by this request.
     */
    void Release();

    /**
     * Abort the request.
     */
    void Abort();

    void Put(unsigned rubber_id);

    void OnTimeout() {
        /* reading the response has taken too long already; don't store
           this resource */
        cache_log(4, "nfs_cache: timeout %s\n", key);
        Abort();
    }

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(unsigned rubber_id, size_t size) override;
    void RubberOutOfMemory() override;
    void RubberTooLarge() override;
    void RubberError(GError *error) override;
};

struct NfsCacheItem {
    struct cache_item item;

    struct pool &pool;

    struct stat stat;

    Rubber &rubber;
    unsigned rubber_id;

    NfsCacheItem(struct pool &_pool, const NfsCacheStore &store,
                 Rubber &_rubber, unsigned _rubber_id)
        :pool(_pool), stat(store.stat),
         rubber(_rubber), rubber_id(_rubber_id) {
        cache_item_init_relative(&item, 60, stat.st_size);
    }
};

static constexpr off_t cacheable_size_limit = 512 * 1024;

static constexpr struct timeval nfs_cache_timeout = { 60, 0 };

static const char *
nfs_cache_key(struct pool &pool, const char *server,
              const char *_export, const char *path)
{
    return p_strcat(&pool, server, ":", _export, path, nullptr);
}

static void
nfs_cache_request_error(GError *error, void *ctx)
{
    NfsCacheRequest *r = (NfsCacheRequest *)ctx;

    r->handler.error(error, r->handler_ctx);
}

void
NfsCacheStore::Release()
{
    assert(!async_ref.IsDefined());

    timeout_event.Deinit();

    list_remove(&siblings);
    pool_unref(&pool);
}

void
NfsCacheStore::Abort()
{
    assert(async_ref.IsDefined());

    async_ref.Abort();
    async_ref.Clear();
    Release();
}

void
NfsCacheStore::Put(unsigned rubber_id)
{
    cache_log(4, "nfs_cache: put %s\n", key);

    struct pool *item_pool = pool_new_libc(&cache.pool, "nfs_cache_item");
    const auto item = NewFromPool<NfsCacheItem>(*item_pool, *item_pool, *this,
                                                cache.rubber, rubber_id);
    cache_put(&cache.cache, p_strdup(item_pool, key), &item->item);
}

/*
 * sink_rubber_handler
 *
 */

void
NfsCacheStore::RubberDone(unsigned rubber_id, gcc_unused size_t size)
{
    assert((off_t)size == stat.st_size);

    async_ref.Clear();

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    Put(rubber_id);

    Release();
}

void
NfsCacheStore::RubberOutOfMemory()
{
    async_ref.Clear();

    cache_log(4, "nfs_cache: nocache oom %s\n", key);
    Release();
}

void
NfsCacheStore::RubberTooLarge()
{
    async_ref.Clear();

    cache_log(4, "nfs_cache: nocache too large %s\n", key);
    Release();
}

void
NfsCacheStore::RubberError(GError *error)
{
    async_ref.Clear();

    cache_log(4, "nfs_cache: body_abort %s: %s\n", key, error->message);
    g_error_free(error);

    Release();
}

/*
 * NfsClientOpenFileHandler
 *
 */

static void
nfs_open_ready(NfsFileHandle *handle, const struct stat *st,
               void *ctx)
{
    auto &r = *(NfsCacheRequest *)ctx;

    NfsCacheHandle handle2 = {
        .cache = r.cache,
        .key = r.key,
        .file = handle,
        .item = nullptr,
        .stat = *st,
    };

    r.handler.response(handle2, *st, r.handler_ctx);

    if (handle2.file != nullptr)
        nfs_client_close_file(handle2.file);
}

static constexpr NfsClientOpenFileHandler nfs_open_handler = {
    .ready = nfs_open_ready,
    .error = nfs_cache_request_error,
};

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_cache_request_stock_ready(NfsClient *client, void *ctx)
{
    auto &r = *(NfsCacheRequest *)ctx;

    nfs_client_open_file(client, &r.pool, r.path,
                         &nfs_open_handler, &r, &r.async_ref);
}

static constexpr NfsStockGetHandler nfs_cache_request_stock_handler = {
    .ready = nfs_cache_request_stock_ready,
    .error = nfs_cache_request_error,
};

/*
 * cache_class
 *
 */

static bool
nfs_cache_item_validate(struct cache_item *_item)
{
    const auto &item = *(NfsCacheItem *)_item;

    (void)item;
    return true;
}

static void
nfs_cache_item_destroy(struct cache_item *_item)
{
    const auto &item = *(NfsCacheItem *)_item;

    if (item.rubber_id != 0)
        rubber_remove(&item.rubber, item.rubber_id);

    pool_unref(&item.pool);
}

static const struct cache_class nfs_cache_class = {
    .validate = nfs_cache_item_validate,
    .destroy = nfs_cache_item_destroy,
};

/*
 * constructor
 *
 */

static Rubber &
NewRubberOrAbort(size_t max_size)
{
    auto r = rubber_new(max_size);
    if (r == nullptr) {
        fprintf(stderr, "Failed to allocate HTTP cache: %s\n",
                strerror(errno));
        exit(2);
    }

    return *r;
}

inline
NfsCache::NfsCache(struct pool &_pool, size_t max_size,
                   NfsStock &_stock, EventLoop &_event_loop)
    :pool(*pool_new_libc(&_pool, "nfs_cache")), stock(_stock),
     event_loop(_event_loop),
     cache(*cache_new(pool, &nfs_cache_class, 65521, max_size * 7 / 8)),
     compress_timer(MakeSimpleEventCallback(NfsCache, OnCompressTimer), this),
     rubber(NewRubberOrAbort(max_size)) {
    list_init(&requests);

    compress_timer.Add(nfs_cache_compress_interval);
}

NfsCache *
nfs_cache_new(struct pool &_pool, size_t max_size,
              NfsStock &stock, EventLoop &event_loop)
{
    return new NfsCache(_pool, max_size, stock, event_loop);
}

void
nfs_cache_free(NfsCache *cache)
{
    assert(cache != nullptr);

    delete cache;
}

AllocatorStats
nfs_cache_get_stats(const NfsCache &cache)
{
    return cache_get_stats(cache.cache) + rubber_get_stats(cache.rubber);
}

void
nfs_cache_fork_cow(NfsCache &cache, bool inherit)
{
    rubber_fork_cow(&cache.rubber, inherit);
}

void
nfs_cache_request(struct pool &pool, NfsCache &cache,
                  const char *server, const char *_export, const char *path,
                  const NfsCacheHandler &handler, void *ctx,
                  struct async_operation_ref &async_ref)
{
    const char *key = nfs_cache_key(pool, server, _export, path);
    const auto item = (NfsCacheItem *)cache_get(&cache.cache, key);
    if (item != nullptr) {
        cache_log(4, "nfs_cache: hit %s\n", key);

        NfsCacheHandle handle2 = {
            .cache = cache,
            .key = key,
            .file = nullptr,
            .item = item,
            .stat = item->stat,
        };

        handler.response(handle2, item->stat, ctx);
        return;
    }

    cache_log(4, "nfs_cache: miss %s\n", key);

    auto r = NewFromPool<NfsCacheRequest>(pool, pool, cache,
                                          key, path,
                                          handler, ctx, async_ref);
    nfs_stock_get(&cache.stock, &pool, server, _export,
                  &nfs_cache_request_stock_handler, r,
                  &async_ref);
}

static Istream *
nfs_cache_item_open(struct pool &pool, NfsCache &cache,
                    NfsCacheItem &item,
                    uint64_t start, uint64_t end)
{
    assert(start <= end);
    assert(end <= (uint64_t)item.stat.st_size);

    assert(item.rubber_id != 0);

    Istream *istream =
        istream_rubber_new(pool, item.rubber, item.rubber_id,
                           start, end, false);
    return istream_unlock_new(pool, *istream, cache.cache, item.item);
}

static Istream *
nfs_cache_file_open(struct pool &pool, NfsCache &cache,
                    const char *key,
                    NfsFileHandle &file, const struct stat &st,
                    uint64_t start, uint64_t end)
{
    assert(start <= end);
    assert(end <= (uint64_t)st.st_size);

    Istream *body = istream_nfs_new(pool, file, start, end);
    if (st.st_size > cacheable_size_limit || start != 0 ||
        end != (uint64_t)st.st_size) {
        /* don't cache */
        cache_log(4, "nfs_cache: nocache %s\n", key);
        return body;
    }

    /* move all this stuff to a new pool, so istream_tee's second head
       can continue to fill the cache even if our caller gave up on
       it */
    struct pool *pool2 = pool_new_linear(&cache.pool,
                                         "nfs_cache_tee", 1024);
    auto store = NewFromPool<NfsCacheStore>(*pool2, *pool2, cache,
                                            p_strdup(pool2, key), st);

    /* tee the body: one goes to our client, and one goes into the
       cache */
    body = istream_tee_new(*pool2, *body,
                           cache.event_loop,
                           false, true);

    list_add(&store->siblings, &cache.requests);

    store->timeout_event.Add(nfs_cache_timeout);

    sink_rubber_new(*pool2, istream_tee_second(*body),
                    cache.rubber, cacheable_size_limit,
                    *store,
                    store->async_ref);

    return body;
}

Istream *
nfs_cache_handle_open(struct pool &pool, NfsCacheHandle &handle,
                      uint64_t start, uint64_t end)
{
    assert((handle.file == nullptr) != (handle.item == nullptr));
    assert(start <= end);
    assert(end <= (uint64_t)handle.stat.st_size);

    if (start == end)
        return istream_null_new(&pool);

    if (handle.item != nullptr) {
        /* cache hit: serve cached file */
        cache_log(5, "nfs_cache: serve %s\n", handle.key);
        return nfs_cache_item_open(pool, handle.cache, *handle.item,
                                   start, end);
    } else {
        /* cache miss: load from NFS server */
        NfsFileHandle *const file = handle.file;
        handle.file = nullptr;

        return nfs_cache_file_open(pool, handle.cache, handle.key,
                                   *file, handle.stat, start, end);
    }
}
