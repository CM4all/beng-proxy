/*
 * A cache for NFS files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_cache.hxx"
#include "nfs_stock.hxx"
#include "nfs_client.hxx"
#include "istream_nfs.hxx"
#include "istream.h"
#include "strmap.hxx"
#include "pool.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "istream_null.hxx"
#include "istream_rubber.hxx"
#include "istream_tee.h"
#include "cache.hxx"
#include "async.hxx"

#include <inline/list.h>

#include <event.h>

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

struct nfs_cache {
    struct pool *pool;

    struct nfs_stock *stock;

    struct cache *cache;

    Rubber *rubber;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    struct list_head requests;
};

struct NFSCacheRequest {
    struct pool *pool;

    struct nfs_cache *cache;

    const char *key;
    const char *path;

    const struct nfs_cache_handler *handler;
    void *handler_ctx;
    struct async_operation_ref *async_ref;
};

struct nfs_cache_handle {
    struct nfs_cache *cache;
    const char *key;

    struct nfs_file_handle *file;
    nfs_cache_item *item;
    const struct stat *stat;
};

struct NFSCacheStore {
    struct list_head siblings;

    struct pool *pool;

    struct nfs_cache *cache;

    const char *key;

    struct stat stat;

    struct event timeout_event;
    struct async_operation_ref async_ref;
};

struct nfs_cache_item {
    struct cache_item item;

    struct pool *pool;

    struct stat stat;

    Rubber *rubber;
    unsigned rubber_id;
};

static constexpr off_t cacheable_size_limit = 256 * 1024;

static constexpr struct timeval nfs_cache_timeout = { 60, 0 };

static const char *
nfs_cache_key(struct pool *pool, const char *server,
              const char *_export, const char *path)
{
    return p_strcat(pool, server, ":", _export, path, nullptr);
}

static void
nfs_cache_request_error(GError *error, void *ctx)
{
    NFSCacheRequest *r = (NFSCacheRequest *)ctx;

    r->handler->error(error, r->handler_ctx);
}

/**
 * Release resources held by this request.
 */
static void
nfs_cache_store_release(NFSCacheStore *store)
{
    assert(store != nullptr);
    assert(!store->async_ref.IsDefined());

    evtimer_del(&store->timeout_event);

    list_remove(&store->siblings);
    pool_unref(store->pool);
}

/**
 * Abort the request.
 */
static void
nfs_cache_store_abort(NFSCacheStore *store)
{
    assert(store != nullptr);
    assert(store->async_ref.IsDefined());

    store->async_ref.Abort();
    store->async_ref.Clear();
    nfs_cache_store_release(store);
}

static void
nfs_cache_put(NFSCacheStore *store, unsigned rubber_id)
{
    struct nfs_cache *const cache = store->cache;

    cache_log(4, "nfs_cache: put %s\n", store->key);

    struct pool *pool = pool_new_libc(cache->pool, "nfs_cache_item");
    nfs_cache_item *item = NewFromPool<nfs_cache_item>(*pool);
    item->pool = pool;
    item->stat = store->stat;
    item->rubber = cache->rubber;
    item->rubber_id = rubber_id;

    cache_item_init_relative(&item->item, 60, item->stat.st_size);

    cache_put(cache->cache, p_strdup(pool, store->key), &item->item);
}

/*
 * sink_rubber_handler
 *
 */

static void
nfs_cache_rubber_done(unsigned rubber_id, gcc_unused size_t size, void *ctx)
{
    NFSCacheStore *store = (NFSCacheStore *)ctx;
    assert((off_t)size == store->stat.st_size);

    store->async_ref.Clear();

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    nfs_cache_put(store, rubber_id);

    nfs_cache_store_release(store);
}

static void
nfs_cache_rubber_no_store(void *ctx)
{
    NFSCacheStore *store = (NFSCacheStore *)ctx;
    store->async_ref.Clear();

    cache_log(4, "nfs_cache: nocache %s\n", store->key);
    nfs_cache_store_release(store);
}

static void
nfs_cache_rubber_error(GError *error, void *ctx)
{
    NFSCacheStore *store = (NFSCacheStore *)ctx;
    store->async_ref.Clear();

    cache_log(4, "nfs_cache: body_abort %s: %s\n", store->key, error->message);
    g_error_free(error);

   nfs_cache_store_release(store);
}

static const struct sink_rubber_handler nfs_cache_rubber_handler = {
    .done = nfs_cache_rubber_done,
    .out_of_memory = nfs_cache_rubber_no_store,
    .too_large = nfs_cache_rubber_no_store,
    .error = nfs_cache_rubber_error,
};

/*
 * nfs_client_open_file_handler
 *
 */

static void
nfs_open_ready(struct nfs_file_handle *handle, const struct stat *st,
               void *ctx)
{
    NFSCacheRequest *r = (NFSCacheRequest *)ctx;

    struct nfs_cache_handle handle2 = {
        .cache = r->cache,
        .key = r->key,
        .file = handle,
        .item = nullptr,
        .stat = st,
    };

    r->handler->response(&handle2, st, r->handler_ctx);

    if (handle2.file != nullptr)
        nfs_client_close_file(handle2.file);
}

static const struct nfs_client_open_file_handler nfs_open_handler = {
    .ready = nfs_open_ready,
    .error = nfs_cache_request_error,
};

/*
 * nfs_stock_get_handler
 *
 */

static void
nfs_cache_request_stock_ready(struct nfs_client *client, void *ctx)
{
    NFSCacheRequest *r = (NFSCacheRequest *)ctx;

    nfs_client_open_file(client, r->pool, r->path,
                         &nfs_open_handler, r, r->async_ref);
}

static const struct nfs_stock_get_handler nfs_cache_request_stock_handler = {
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
    nfs_cache_item *item = (nfs_cache_item *)_item;

    (void)item;
    return true;
}

static void
nfs_cache_item_destroy(struct cache_item *_item)
{
    nfs_cache_item *item = (nfs_cache_item *)_item;

    if (item->rubber_id != 0)
        rubber_remove(item->rubber, item->rubber_id);

    pool_unref(item->pool);
}

static const struct cache_class nfs_cache_class = {
    .validate = nfs_cache_item_validate,
    .destroy = nfs_cache_item_destroy,
};

/*
 * libevent callbacks
 *
 */

static void
nfs_cache_timeout_callback(gcc_unused int fd, gcc_unused short event,
                           void *ctx)
{
    NFSCacheStore *store = (NFSCacheStore *)ctx;

    /* reading the response has taken too long already; don't store
       this resource */
    cache_log(4, "nfs_cache: timeout %s\n", store->key);
    nfs_cache_store_abort(store);
}

/*
 * constructor
 *
 */

struct nfs_cache *
nfs_cache_new(struct pool *pool, size_t max_size,
              struct nfs_stock *stock)
{
    pool = pool_new_libc(pool, "nfs_cache");
    nfs_cache *cache = NewFromPool<nfs_cache>(*pool);
    cache->pool = pool;
    cache->stock = stock;
    cache->cache = cache_new(*pool, &nfs_cache_class, 65521, max_size * 7 / 8);

    cache->rubber = rubber_new(max_size);
    if (cache->rubber == nullptr) {
        fprintf(stderr, "Failed to allocate HTTP cache: %s\n",
                strerror(errno));
        exit(2);
    }

    list_init(&cache->requests);

    return cache;
}

void
nfs_cache_free(struct nfs_cache *cache)
{
    assert(cache != nullptr);
    assert(cache->cache != nullptr);
    assert(cache->stock != nullptr);

    cache_close(cache->cache);
    rubber_free(cache->rubber);
    pool_unref(cache->pool);
}

void
nfs_cache_request(struct pool *pool, struct nfs_cache *cache,
                  const char *server, const char *_export, const char *path,
                  const struct nfs_cache_handler *handler, void *ctx,
                  struct async_operation_ref *async_ref)
{
    const char *key = nfs_cache_key(pool, server, _export, path);
    nfs_cache_item *item = (nfs_cache_item *)
        cache_get(cache->cache, key);
    if (item != nullptr) {
        cache_log(4, "nfs_cache: hit %s\n", key);

        struct nfs_cache_handle handle2 = {
            .cache = cache,
            .key = key,
            .file = nullptr,
            .item = item,
            .stat = &item->stat,
        };

        handler->response(&handle2, &item->stat, ctx);
        return;
    }

    cache_log(4, "nfs_cache: miss %s\n", key);

    NFSCacheRequest *r = NewFromPool<NFSCacheRequest>(*pool);

    r->pool = pool;
    r->cache = cache;
    r->key = key;
    r->path = path;
    r->handler = handler;
    r->handler_ctx = ctx;
    r->async_ref = async_ref;

    nfs_stock_get(cache->stock, pool, server, _export,
                  &nfs_cache_request_stock_handler, r,
                  async_ref);
}

static struct istream *
nfs_cache_item_open(struct pool *pool, struct nfs_cache *cache,
                    nfs_cache_item *item,
                    uint64_t start, uint64_t end)
{
    assert(cache != nullptr);
    assert(item != nullptr);
    assert(start <= end);
    assert(end <= (uint64_t)item->stat.st_size);

    assert(item->rubber_id != 0);

    struct istream *istream =
        istream_rubber_new(pool, item->rubber, item->rubber_id,
                           start, end, false);
    return istream_unlock_new(pool, istream, cache->cache, &item->item);
}

static struct istream *
nfs_cache_file_open(struct pool *pool, struct nfs_cache *cache,
                    const char *key,
                    struct nfs_file_handle *file, const struct stat *st,
                    uint64_t start, uint64_t end)
{
    assert(cache != nullptr);
    assert(file != nullptr);
    assert(start <= end);
    assert(end <= (uint64_t)st->st_size);

    struct istream *body = istream_nfs_new(pool, file, start, end);
    if (st->st_size > cacheable_size_limit || start != 0 ||
        end != (uint64_t)st->st_size) {
        /* don't cache */
        cache_log(4, "nfs_cache: nocache %s\n", key);
        return body;
    }

    /* move all this stuff to a new pool, so istream_tee's second head
       can continue to fill the cache even if our caller gave up on
       it */
    struct pool *pool2 = pool_new_linear(cache->pool,
                                         "nfs_cache_tee", 1024);
    NFSCacheStore *store = NewFromPool<NFSCacheStore>(*pool2);
    store->pool = pool2;
    store->cache = cache;
    store->key = p_strdup(pool2, key);
    store->stat = *st;

    /* tee the body: one goes to our client, and one goes into the
       cache */
    body = istream_tee_new(pool2, body, false, true);

    list_add(&store->siblings, &cache->requests);

    evtimer_set(&store->timeout_event, nfs_cache_timeout_callback, store);
    evtimer_add(&store->timeout_event, &nfs_cache_timeout);

    sink_rubber_new(pool2, istream_tee_second(body),
                    cache->rubber, cacheable_size_limit,
                    &nfs_cache_rubber_handler, store,
                    &store->async_ref);

    return body;
}

struct istream *
nfs_cache_handle_open(struct pool *pool, struct nfs_cache_handle *handle,
                      uint64_t start, uint64_t end)
{
    assert((handle->file == nullptr) != (handle->item == nullptr));
    assert(start <= end);
    assert(end <= (uint64_t)handle->stat->st_size);

    if (start == end)
        return istream_null_new(pool);

    if (handle->item != nullptr) {
        /* cache hit: serve cached file */
        cache_log(5, "nfs_cache: serve %s\n", handle->key);
        return nfs_cache_item_open(pool, handle->cache, handle->item,
                                   start, end);
    } else {
        /* cache miss: load from NFS server */
        struct nfs_file_handle *const file = handle->file;
        handle->file = nullptr;

        return nfs_cache_file_open(pool, handle->cache, handle->key,
                                   file, handle->stat, start, end);
    }
}
