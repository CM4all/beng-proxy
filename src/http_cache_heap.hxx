/*
 * Caching HTTP responses in heap memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_HEAP_HXX
#define BENG_PROXY_HTTP_CACHE_HEAP_HXX

#include <inline/compiler.h>
#include <http/status.h>

#include <stddef.h>

struct pool;
class Istream;
class Rubber;
class EventLoop;
struct Cache;
struct strmap;
struct AllocatorStats;
struct HttpCacheResponseInfo;
struct SlicePool;
struct HttpCacheDocument;

class HttpCacheHeap {
    struct pool *pool;

    Cache *cache;

    SlicePool *slice_pool;

public:
    void Init(struct pool &pool, EventLoop &event_loop, size_t max_size);
    void Deinit();

    void ForkCow(bool inherit);

    void Clear() {
        cache = nullptr;
    }

    bool IsDefined() const {
        return cache != nullptr;
    }

    gcc_pure
    AllocatorStats GetStats(const Rubber &rubber) const;

    HttpCacheDocument *Get(const char *uri, struct strmap *request_headers);

    void Put(const char *url,
             const HttpCacheResponseInfo &info,
             struct strmap *request_headers,
             http_status_t status,
             const struct strmap *response_headers,
             Rubber &rubber, unsigned rubber_id, size_t size);

    void Remove(HttpCacheDocument &document);
    void RemoveURL(const char *url, struct strmap *headers);

    void Compress();
    void Flush();

    static void Lock(HttpCacheDocument &document);
    void Unlock(HttpCacheDocument &document);

    Istream *OpenStream(struct pool &_pool, HttpCacheDocument &document);
};

#endif
