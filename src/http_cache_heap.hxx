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
class Rubber;
struct strmap;
struct AllocatorStats;
struct http_cache_response_info;

struct http_cache_heap {
    struct pool *pool;

    struct cache *cache;

    struct slice_pool *slice_pool;

    void Init(struct pool &pool, size_t max_size);
    void Deinit();

    void Clear() {
        cache = nullptr;
    }

    bool IsDefined() const {
        return cache != nullptr;
    }

    gcc_pure
    AllocatorStats GetStats(const Rubber &rubber) const;

    struct http_cache_document *Get(const char *uri,
                                    struct strmap *request_headers);

    void Put(const char *url,
             const struct http_cache_response_info &info,
             struct strmap *request_headers,
             http_status_t status,
             const struct strmap *response_headers,
             Rubber &rubber, unsigned rubber_id, size_t size);

    void Remove(const char *url, struct http_cache_document &document);
    void RemoveURL(const char *url, struct strmap *headers);

    void Flush();

    static void Lock(struct http_cache_document &document);
    void Unlock(struct http_cache_document &document);

    struct istream *OpenStream(struct pool &_pool,
                               struct http_cache_document &document);
};

#endif
