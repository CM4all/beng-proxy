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
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "rubber.hxx"
#include "sink_rubber.hxx"
#include "istream_unlock.hxx"
#include "istream_rubber.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_tee.hxx"
#include "AllocatorStats.hxx"
#include "cache.hxx"
#include "event/TimerEvent.hxx"
#include "event/Loop.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/LeakDetector.hxx"

#include <boost/intrusive/list.hpp>

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static constexpr Event::Duration nfs_cache_compress_interval = std::chrono::minutes(10);

class NfsCache;
struct NfsCacheItem;
struct NfsCacheStore;

struct NfsCacheStore final
    : PoolHolder,
      public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>,
      RubberSinkHandler, LeakDetector {

    NfsCache &cache;

    const char *key;

    struct stat stat;

    TimerEvent timeout_event;
    CancellablePointer cancel_ptr;

    NfsCacheStore(PoolPtr &&_pool, NfsCache &_cache,
                  const char *_key, const struct stat &_st);

    ~NfsCacheStore() noexcept;

    /**
     * Release resources held by this request.
     */
    void Destroy() noexcept {
        this->~NfsCacheStore();
    }

    using PoolHolder::GetPool;

    /**
     * Abort the request.
     */
    void Abort() noexcept;

    void Put(RubberAllocation &&a) noexcept;

    void OnTimeout() noexcept {
        /* reading the response has taken too long already; don't store
           this resource */
        LogConcat(4, "NfsCache", "timeout ", key);
        Abort();
    }

    /* virtual methods from class RubberSinkHandler */
    void RubberDone(RubberAllocation &&a, size_t size) noexcept override;
    void RubberOutOfMemory() noexcept override;
    void RubberTooLarge() noexcept override;
    void RubberError(std::exception_ptr ep) noexcept override;
};

class NfsCache {
    const PoolPtr pool;

    NfsStock &stock;
    EventLoop &event_loop;

    Rubber rubber;

    Cache cache;

    TimerEvent compress_timer;

    /**
     * A list of requests that are currently saving their contents to
     * the cache.
     */
    boost::intrusive::list<NfsCacheStore,
                           boost::intrusive::constant_time_size<false>> requests;

public:
    NfsCache(struct pool &_pool, size_t max_size, NfsStock &_stock,
             EventLoop &_event_loop);

    auto &GetPool() const noexcept {
        return pool;
    }

    auto &GetEventLoop() const noexcept {
        return event_loop;
    }

    auto &GetRubber() noexcept {
        return rubber;
    }

    void ForkCow(bool inherit) noexcept {
        rubber.ForkCow(inherit);
    }

    void Flush() {
        cache.Flush();
        rubber.Compress();
    }

    auto GetStats() const noexcept {
        return pool_children_stats(pool) + rubber.GetStats();
    }

    void Put(const char *key, CacheItem &item) noexcept {
        cache.Put(key, item);
    }

    void Request(struct pool &caller_pool,
                 const char *server, const char *_export, const char *path,
                 NfsCacheHandler &handler,
                 CancellablePointer &cancel_ptr) noexcept;

    UnusedIstreamPtr OpenFile(struct pool &caller_pool,
                              const char *key,
                              NfsFileHandle &file, const struct stat &st,
                              uint64_t start, uint64_t end) noexcept;

private:
    void OnCompressTimer() noexcept {
        rubber.Compress();
        compress_timer.Schedule(nfs_cache_compress_interval);
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
         key(p_strdup(pool, _key)), path(_path),
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

struct NfsCacheItem final : PoolHolder, CacheItem {
    struct stat stat;

    const RubberAllocation body;

    NfsCacheItem(PoolPtr &&_pool,
                 std::chrono::steady_clock::time_point now,
                 const NfsCacheStore &store,
                 RubberAllocation &&_body) noexcept
        :PoolHolder(std::move(_pool)),
         CacheItem(now, std::chrono::minutes(1), store.stat.st_size),
         stat(store.stat),
         body(std::move(_body)) {
    }

    using PoolHolder::GetPool;

    /* virtual methods from class CacheItem */
    void Destroy() noexcept override {
        pool_trash(pool);
        this->~NfsCacheItem();
    }
};

static constexpr off_t cacheable_size_limit = 512 * 1024;

static constexpr Event::Duration nfs_cache_timeout = std::chrono::minutes(1);

static const char *
nfs_cache_key(struct pool &pool, const char *server,
              const char *_export, const char *path)
{
    return p_strcat(&pool, server, ":", _export, path, nullptr);
}

NfsCacheStore::NfsCacheStore(PoolPtr &&_pool, NfsCache &_cache,
                             const char *_key, const struct stat &_st)
    :PoolHolder(std::move(_pool)), cache(_cache),
     key(_key),
     stat(_st),
     timeout_event(cache.GetEventLoop(), BIND_THIS_METHOD(OnTimeout)) {}

NfsCacheStore::~NfsCacheStore() noexcept
{
    assert(!cancel_ptr);
}

void
NfsCacheStore::Abort() noexcept
{
    assert(cancel_ptr);

    cancel_ptr.CancelAndClear();
    Destroy();
}

void
NfsCacheStore::Put(RubberAllocation &&a) noexcept
{
    LogConcat(4, "NfsCache", "put ", key);

    const auto item = NewFromPool<NfsCacheItem>(pool_new_libc(cache.GetPool(),
                                                              "NfsCacheItem"),
                                                cache.GetEventLoop().SteadyNow(),
                                                *this,
                                                std::move(a));
    cache.Put(p_strdup(item->GetPool(), key), *item);
}

/*
 * sink_rubber_handler
 *
 */

void
NfsCacheStore::RubberDone(RubberAllocation &&a, gcc_unused size_t size) noexcept
{
    assert((off_t)size == stat.st_size);

    cancel_ptr = nullptr;

    /* the request was successful, and all of the body data has been
       saved: add it to the cache */
    Put(std::move(a));

    Destroy();
}

void
NfsCacheStore::RubberOutOfMemory() noexcept
{
    cancel_ptr = nullptr;

    LogConcat(4, "NfsCache", "nocache oom ", key);
    Destroy();
}

void
NfsCacheStore::RubberTooLarge() noexcept
{
    cancel_ptr = nullptr;

    LogConcat(4, "NfsCache", "nocache too large ", key);
    Destroy();
}

void
NfsCacheStore::RubberError(std::exception_ptr ep) noexcept
{
    cancel_ptr = nullptr;

    LogConcat(4, "NfsCache", "body_abort ", key, ": ", ep);

    Destroy();
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
    nfs_client_open_file(client, path,
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

inline
NfsCache::NfsCache(struct pool &_pool, size_t max_size,
                   NfsStock &_stock, EventLoop &_event_loop)
    :pool(pool_new_libc(&_pool, "nfs_cache")),
     stock(_stock),
     event_loop(_event_loop),
     rubber(max_size),
     cache(event_loop, 65521, max_size * 7 / 8),
     compress_timer(event_loop, BIND_THIS_METHOD(OnCompressTimer)) {
    compress_timer.Schedule(nfs_cache_compress_interval);
}

NfsCache *
nfs_cache_new(struct pool &_pool, size_t max_size,
              NfsStock &stock, EventLoop &event_loop)
{
    return new NfsCache(_pool, max_size, stock, event_loop);
}

void
nfs_cache_free(NfsCache *cache) noexcept
{
    assert(cache != nullptr);

    delete cache;
}

AllocatorStats
nfs_cache_get_stats(const NfsCache &cache) noexcept
{
    return cache.GetStats();
}

void
nfs_cache_fork_cow(NfsCache &cache, bool inherit) noexcept
{
    cache.ForkCow(inherit);
}

void
nfs_cache_flush(NfsCache &cache) noexcept
{
    cache.Flush();
}

inline void
NfsCache::Request(struct pool &caller_pool,
                  const char *server, const char *_export, const char *path,
                  NfsCacheHandler &handler,
                  CancellablePointer &cancel_ptr) noexcept
{
    const char *key = nfs_cache_key(caller_pool, server, _export, path);
    const auto item = (NfsCacheItem *)cache.Get(key);
    if (item != nullptr) {
        LogConcat(4, "NfsCache", "hit ", key);

        NfsCacheHandle handle2 = {
            .cache = *this,
            .key = key,
            .file = nullptr,
            .item = item,
            .stat = item->stat,
        };

        handler.OnNfsCacheResponse(handle2, item->stat);
        return;
    }

    LogConcat(4, "NfsCache", "miss ", key);

    auto r = NewFromPool<NfsCacheRequest>(caller_pool, caller_pool, *this,
                                          key, path,
                                          handler, cancel_ptr);
    nfs_stock_get(&stock, &caller_pool, server, _export,
                  *r, cancel_ptr);
}

void
nfs_cache_request(struct pool &pool, NfsCache &cache,
                  const char *server, const char *_export, const char *path,
                  NfsCacheHandler &handler,
                  CancellablePointer &cancel_ptr) noexcept
{
    cache.Request(pool, server, _export, path, handler, cancel_ptr);
}

static UnusedIstreamPtr
nfs_cache_item_open(struct pool &pool,
                    NfsCacheItem &item,
                    uint64_t start, uint64_t end) noexcept
{
    assert(start <= end);
    assert(end <= (uint64_t)item.stat.st_size);

    assert(item.body);

    return istream_unlock_new(pool,
                              istream_rubber_new(pool,
                                                 item.body.GetRubber(),
                                                 item.body.GetId(),
                                                 start, end, false),
                              item);
}

inline UnusedIstreamPtr
NfsCache::OpenFile(struct pool &caller_pool,
                   const char *key,
                   NfsFileHandle &file, const struct stat &st,
                   uint64_t start, uint64_t end) noexcept
{
    assert(start <= end);
    assert(end <= (uint64_t)st.st_size);

    auto body = istream_nfs_new(caller_pool, file, start, end);
    if (st.st_size > cacheable_size_limit || start != 0 ||
        end != (uint64_t)st.st_size) {
        /* don't cache */
        LogConcat(4, "NfsCache", "nocache ", key);
        return body;
    }

    /* move all this stuff to a new pool, so istream_tee's second head
       can continue to fill the cache even if our caller gave up on
       it */
    auto store = NewFromPool<NfsCacheStore>(pool_new_linear(pool, "nfs_cache_tee", 1024),
                                            *this,
                                            key, st);

    /* tee the body: one goes to our client, and one goes into the
       cache */
    auto tee = istream_tee_new(store->GetPool(), std::move(body),
                               event_loop,
                               false, true,
                               /* just in case our handler closes the
                                  body without looking at it: defer
                                  an Istream::Read() call for the
                                  Rubber sink */
                               true);

    requests.push_back(*store);

    store->timeout_event.Schedule(nfs_cache_timeout);

    sink_rubber_new(store->GetPool(), std::move(tee.second),
                    rubber, cacheable_size_limit,
                    *store,
                    store->cancel_ptr);

    return std::move(tee.first);
}

UnusedIstreamPtr
nfs_cache_handle_open(struct pool &pool, NfsCacheHandle &handle,
                      uint64_t start, uint64_t end) noexcept
{
    assert((handle.file == nullptr) != (handle.item == nullptr));
    assert(start <= end);
    assert(end <= (uint64_t)handle.stat.st_size);

    if (start == end)
        return istream_null_new(pool);

    if (handle.item != nullptr) {
        /* cache hit: serve cached file */
        LogConcat(5, "NfsCache", "serve ", handle.key);
        return nfs_cache_item_open(pool, *handle.item,
                                   start, end);
    } else {
        /* cache miss: load from NFS server */
        NfsFileHandle *const file = handle.file;
        handle.file = nullptr;

        return handle.cache.OpenFile(pool, handle.key,
                                     *file, handle.stat, start, end);
    }
}
