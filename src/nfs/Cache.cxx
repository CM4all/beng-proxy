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

#include "Cache.hxx"
#include "Stock.hxx"
#include "Client.hxx"
#include "Handler.hxx"
#include "Istream.hxx"
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
#include "event/TimerEvent.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#include <boost/intrusive/list.hpp>

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

struct NfsCache;
struct NfsCacheItem;
struct NfsCacheStore;

struct NfsCacheStore final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      RubberSinkHandler {

    struct pool &pool;

    NfsCache &cache;

    const char *key;

    struct stat stat;

    TimerEvent timeout_event;
    CancellablePointer cancel_ptr;

    NfsCacheStore(struct pool &_pool, NfsCache &_cache,
                  const char *_key, const struct stat &_st);

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
    void RubberError(std::exception_ptr ep) override;
};

struct NfsCache {
    struct pool &pool;

    NfsStock &stock;
    EventLoop &event_loop;

    Cache cache;

    TimerEvent compress_timer;

    Rubber &rubber;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    boost::intrusive::list<NfsCacheStore,
                           boost::intrusive::constant_time_size<false>> requests;

    NfsCache(struct pool &_pool, size_t max_size, NfsStock &_stock,
             EventLoop &_event_loop);

    ~NfsCache() {
        compress_timer.Cancel();
        rubber_free(&rubber);
        pool_unref(&pool);
    }

private:
    void OnCompressTimer() {
        rubber_compress(&rubber);
        compress_timer.Add(nfs_cache_compress_interval);
    }
};

struct NfsCacheRequest final : NfsStockGetHandler, NfsClientOpenFileHandler {
    struct pool &pool;

    NfsCache &cache;

    const char *key;
    const char *path;

    NfsCacheHandler &handler;
    CancellablePointer &cancel_ptr;

    NfsCacheRequest(struct pool &_pool, NfsCache &_cache,
                    const char *_key, const char *_path,
                    NfsCacheHandler &_handler,
                    CancellablePointer &_cancel_ptr)
        :pool(_pool), cache(_cache),
         key(_key), path(_path),
         handler(_handler),
         cancel_ptr(_cancel_ptr) {}

    void Error(std::exception_ptr ep) {
        handler.OnNfsCacheError(ep);
    }

    /* virtual methods from class NfsStockGetHandler */
    void OnNfsStockReady(NfsClient &client) override;
    void OnNfsStockError(std::exception_ptr ep) override;

    /* virtual methods from class NfsClientOpenFileHandler */
    void OnNfsOpen(NfsFileHandle *handle, const struct stat *st) override;
    void OnNfsOpenError(std::exception_ptr ep) override {
        Error(ep);
    }
};

struct NfsCacheHandle {
    NfsCache &cache;
    const char *key;

    NfsFileHandle *file;
    NfsCacheItem *item;
    const struct stat &stat;
};

struct NfsCacheItem final : CacheItem {
    struct pool &pool;

    struct stat stat;

    Rubber &rubber;
    unsigned rubber_id;

    NfsCacheItem(struct pool &_pool, const NfsCacheStore &store,
                 Rubber &_rubber, unsigned _rubber_id)
        :CacheItem(std::chrono::minutes(1), store.stat.st_size),
         pool(_pool), stat(store.stat),
         rubber(_rubber), rubber_id(_rubber_id) {
    }

    /* virtual methods from class CacheItem */
    void Destroy() override {
        if (rubber_id != 0)
            rubber_remove(&rubber, rubber_id);

        pool_unref(&pool);
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

NfsCacheStore::NfsCacheStore(struct pool &_pool, NfsCache &_cache,
                             const char *_key, const struct stat &_st)
    :pool(_pool), cache(_cache),
     key(_key),
     stat(_st),
     timeout_event(cache.event_loop, BIND_THIS_METHOD(OnTimeout)) {}

void
NfsCacheStore::Release()
{
    assert(!cancel_ptr);

    timeout_event.Cancel();

    cache.requests.erase(cache.requests.iterator_to(*this));
    pool_unref(&pool);
}

void
NfsCacheStore::Abort()
{
    assert(cancel_ptr);

    cancel_ptr.CancelAndClear();
    Release();
}

void
NfsCacheStore::Put(unsigned rubber_id)
{
    cache_log(4, "nfs_cache: put %s\n", key);

    struct pool *item_pool = pool_new_libc(&cache.pool, "nfs_cache_item");
    const auto item = NewFromPool<NfsCacheItem>(*item_pool, *item_pool, *this,
                                                cache.rubber, rubber_id);
    cache.cache.Put(p_strdup(item_pool, key), *item);
}

/*
 * sink_rubber_handler
 *
 */

void
NfsCacheStore::RubberDone(unsigned rubber_id, gcc_unused size_t size)
{
    assert((off_t)size == stat.st_size);

    cancel_ptr = nullptr;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    Put(rubber_id);

    Release();
}

void
NfsCacheStore::RubberOutOfMemory()
{
    cancel_ptr = nullptr;

    cache_log(4, "nfs_cache: nocache oom %s\n", key);
    Release();
}

void
NfsCacheStore::RubberTooLarge()
{
    cancel_ptr = nullptr;

    cache_log(4, "nfs_cache: nocache too large %s\n", key);
    Release();
}

void
NfsCacheStore::RubberError(std::exception_ptr ep)
{
    cancel_ptr = nullptr;

    cache_log(4, "nfs_cache: body_abort %s: %s\n",
              key, GetFullMessage(ep).c_str());

    Release();
}

/*
 * NfsClientOpenFileHandler
 *
 */

void
NfsCacheRequest::OnNfsOpen(NfsFileHandle *handle, const struct stat *st)
{
    NfsCacheHandle handle2 = {
        .cache = cache,
        .key = key,
        .file = handle,
        .item = nullptr,
        .stat = *st,
    };

    handler.OnNfsCacheResponse(handle2, *st);

    if (handle2.file != nullptr)
        nfs_client_close_file(*handle2.file);
}

/*
 * nfs_stock_get_handler
 *
 */

void
NfsCacheRequest::OnNfsStockReady(NfsClient &client)
{
    nfs_client_open_file(client, pool, path,
                         *this, cancel_ptr);
}

void
NfsCacheRequest::OnNfsStockError(std::exception_ptr ep)
{
    Error(ep);
}

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
     cache(event_loop, 65521, max_size * 7 / 8),
     compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)),
     rubber(NewRubberOrAbort(max_size)) {
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
    return pool_children_stats(cache.pool) + rubber_get_stats(cache.rubber);
}

void
nfs_cache_fork_cow(NfsCache &cache, bool inherit)
{
    rubber_fork_cow(&cache.rubber, inherit);
}

void
nfs_cache_request(struct pool &pool, NfsCache &cache,
                  const char *server, const char *_export, const char *path,
                  NfsCacheHandler &handler,
                  CancellablePointer &cancel_ptr)
{
    const char *key = nfs_cache_key(pool, server, _export, path);
    const auto item = (NfsCacheItem *)cache.cache.Get(key);
    if (item != nullptr) {
        cache_log(4, "nfs_cache: hit %s\n", key);

        NfsCacheHandle handle2 = {
            .cache = cache,
            .key = key,
            .file = nullptr,
            .item = item,
            .stat = item->stat,
        };

        handler.OnNfsCacheResponse(handle2, item->stat);
        return;
    }

    cache_log(4, "nfs_cache: miss %s\n", key);

    auto r = NewFromPool<NfsCacheRequest>(pool, pool, cache,
                                          key, path,
                                          handler, cancel_ptr);
    nfs_stock_get(&cache.stock, &pool, server, _export,
                  *r, cancel_ptr);
}

static Istream *
nfs_cache_item_open(struct pool &pool,
                    NfsCacheItem &item,
                    uint64_t start, uint64_t end)
{
    assert(start <= end);
    assert(end <= (uint64_t)item.stat.st_size);

    assert(item.rubber_id != 0);

    Istream *istream =
        istream_rubber_new(pool, item.rubber, item.rubber_id,
                           start, end, false);
    return istream_unlock_new(pool, *istream, item);
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

    cache.requests.push_back(*store);

    store->timeout_event.Add(nfs_cache_timeout);

    sink_rubber_new(*pool2, istream_tee_second(*body),
                    cache.rubber, cacheable_size_limit,
                    *store,
                    store->cancel_ptr);

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
        return nfs_cache_item_open(pool, *handle.item,
                                   start, end);
    } else {
        /* cache miss: load from NFS server */
        NfsFileHandle *const file = handle.file;
        handle.file = nullptr;

        return nfs_cache_file_open(pool, handle.cache, handle.key,
                                   *file, handle.stat, start, end);
    }
}
