/*
 * A cache for NFS files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_cache.h"
#include "nfs_stock.h"
#include "nfs_client.h"
#include "istream_nfs.h"
#include "istream.h"
#include "static-headers.h"
#include "strmap.h"
#include "pool.h"
#include "rubber.h"
#include "sink_rubber.h"
#include "istream_rubber.h"
#include "istream_tee.h"
#include "cache.h"
#include "async.h"

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

    struct rubber *rubber;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    struct list_head requests;
};

struct nfs_cache_request {
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
    struct nfs_cache_item *item;
    const struct stat *stat;
};

struct nfs_cache_store {
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

    struct rubber *rubber;
    unsigned rubber_id;
};

static const off_t cacheable_size_limit = 256 * 1024;

static const struct timeval nfs_cache_timeout = { .tv_sec = 60 };

static const char *
nfs_cache_key(struct pool *pool, const char *server,
              const char *export, const char *path)
{
    return p_strcat(pool, server, ":", export, path, NULL);
}

static void
nfs_cache_request_error(GError *error, void *ctx)
{
    struct nfs_cache_request *r = ctx;

    r->handler->error(error, r->handler_ctx);
}

/**
 * Release resources held by this request.
 */
static void
nfs_cache_store_release(struct nfs_cache_store *store)
{
    assert(store != NULL);
    assert(!async_ref_defined(&store->async_ref));

    evtimer_del(&store->timeout_event);

    list_remove(&store->siblings);
    pool_unref(store->pool);
}

/**
 * Abort the request.
 */
static void
nfs_cache_store_abort(struct nfs_cache_store *store)
{
    assert(store != NULL);
    assert(async_ref_defined(&store->async_ref));

    async_abort(&store->async_ref);
    async_ref_clear(&store->async_ref);
    nfs_cache_store_release(store);
}

static void
nfs_cache_put(struct nfs_cache_store *store, unsigned rubber_id)
{
    struct nfs_cache *const cache = store->cache;

    cache_log(4, "nfs_cache: put %s\n", store->key);

    const time_t expires = time(NULL) + 60;

    struct pool *pool = pool_new_libc(cache->pool, "nfs_cache_item");
    struct nfs_cache_item *item = p_malloc(pool, sizeof(*item));
    item->pool = pool;
    item->stat = store->stat;
    item->rubber = cache->rubber;
    item->rubber_id = rubber_id;

    cache_item_init(&item->item, expires, item->stat.st_size);

    cache_put(cache->cache, p_strdup(pool, store->key), &item->item);
}

/*
 * sink_rubber_handler
 *
 */

static void
nfs_cache_rubber_done(unsigned rubber_id, size_t size, void *ctx)
{
    struct nfs_cache_store *store = ctx;
    assert((off_t)size == store->stat.st_size);

    async_ref_clear(&store->async_ref);

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    nfs_cache_put(store, rubber_id);

    nfs_cache_store_release(store);
}

static void
nfs_cache_rubber_no_store(void *ctx)
{
    struct nfs_cache_store *store = ctx;
    async_ref_clear(&store->async_ref);

    cache_log(4, "nfs_cache: nocache %s\n", store->key);
    nfs_cache_store_release(store);
}

static void
nfs_cache_rubber_error(GError *error, void *ctx)
{
    struct nfs_cache_store *store = ctx;
    async_ref_clear(&store->async_ref);

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
    struct nfs_cache_request *r = ctx;

    struct nfs_cache_handle handle2 = {
        .cache = r->cache,
        .key = r->key,
        .file = handle,
        .item = NULL,
        .stat = st,
    };

    r->handler->response(&handle2, st, r->handler_ctx);

    if (handle2.file != NULL)
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
    struct nfs_cache_request *r = ctx;

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
    struct nfs_cache_item *item = (struct nfs_cache_item *)_item;

    (void)item;
    return true;
}

static void
nfs_cache_item_destroy(struct cache_item *_item)
{
    struct nfs_cache_item *item = (struct nfs_cache_item *)_item;

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
    struct nfs_cache_store *store = ctx;

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
    struct nfs_cache *cache = p_malloc(pool, sizeof(*cache));
    cache->pool = pool;
    cache->stock = stock;
    cache->cache = cache_new(pool, &nfs_cache_class, 65521, max_size * 7 / 8);

    cache->rubber = rubber_new(max_size);
    if (cache->rubber == NULL) {
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
    assert(cache != NULL);
    assert(cache->cache != NULL);
    assert(cache->stock != NULL);

    cache_close(cache->cache);
    rubber_free(cache->rubber);
    pool_unref(cache->pool);
}

void
nfs_cache_request(struct pool *pool, struct nfs_cache *cache,
                  const char *server, const char *export, const char *path,
                  const struct nfs_cache_handler *handler, void *ctx,
                  struct async_operation_ref *async_ref)
{
    const char *key = nfs_cache_key(pool, server, export, path);
    struct nfs_cache_item *item = (struct nfs_cache_item *)
        cache_get(cache->cache, key);
    if (item != NULL) {
        cache_log(4, "nfs_cache: hit %s\n", key);

        struct nfs_cache_handle handle2 = {
            .cache = cache,
            .key = key,
            .file = NULL,
            .item = item,
            .stat = &item->stat,
        };

        handler->response(&handle2, &item->stat, ctx);
        return;
    }

    cache_log(4, "nfs_cache: miss %s\n", key);

    struct nfs_cache_request *r = p_malloc(pool, sizeof(*r));

    r->pool = pool;
    r->cache = cache;
    r->key = key;
    r->path = path;
    r->handler = handler;
    r->handler_ctx = ctx;
    r->async_ref = async_ref;

    nfs_stock_get(cache->stock, pool, server, export,
                  &nfs_cache_request_stock_handler, r,
                  async_ref);
}

static struct istream *
nfs_cache_item_open(struct pool *pool, struct nfs_cache *cache,
                    struct nfs_cache_item *item,
                    uint64_t start, uint64_t end)
{
    assert(cache != NULL);
    assert(item != NULL);
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
    assert(cache != NULL);
    assert(file != NULL);
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
    struct nfs_cache_store *store = p_malloc(pool2, sizeof(*store));
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
    assert((handle->file == NULL) != (handle->item == NULL));
    assert(start <= end);
    assert(end <= (uint64_t)handle->stat->st_size);

    if (start == end)
        return istream_null_new(pool);

    if (handle->item != NULL) {
        /* cache hit: serve cached file */
        cache_log(5, "nfs_cache: serve %s\n", handle->key);
        return nfs_cache_item_open(pool, handle->cache, handle->item,
                                   start, end);
    } else {
        /* cache miss: load from NFS server */
        struct nfs_file_handle *const file = handle->file;
        handle->file = NULL;

        return nfs_cache_file_open(pool, handle->cache, handle->key,
                                   file, handle->stat, start, end);
    }
}
