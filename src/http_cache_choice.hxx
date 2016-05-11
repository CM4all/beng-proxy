/*
 * Caching HTTP responses.  Memcached indirect backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_CHOICE_HXX
#define BENG_PROXY_HTTP_CACHE_CHOICE_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

#include <time.h>

struct pool;
struct HttpCacheChoice;
struct HttpCacheResponseInfo;
struct strmap;
struct MemachedStock;
struct async_operation_ref;

struct HttpCacheChoiceInfo {
    time_t expires;
    const struct strmap *vary;

    gcc_pure
    bool VaryFits(const struct strmap *headers) const;
};

typedef void (*http_cache_choice_get_t)(const char *key, bool unclean,
                                        GError *error, void *ctx);
typedef void (*http_cache_choice_commit_t)(GError *error, void *ctx);
typedef bool (*http_cache_choice_filter_t)(const HttpCacheChoiceInfo *info,
                                           GError *error, void *ctx);
typedef void (*http_cache_choice_cleanup_t)(GError *error, void *ctx);
typedef void (*http_cache_choice_delete_t)(GError *error, void *ctx);

const char *
http_cache_choice_vary_key(struct pool &pool, const char *uri,
                           const struct strmap *vary);

void
http_cache_choice_get(struct pool &pool, MemachedStock &stock,
                      const char *uri, const struct strmap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      struct async_operation_ref &async_ref);

HttpCacheChoice *
http_cache_choice_prepare(struct pool &pool, const char *uri,
                          const HttpCacheResponseInfo &info,
                          const struct strmap &vary);

void
http_cache_choice_commit(HttpCacheChoice &choice,
                         MemachedStock &stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         struct async_operation_ref &async_ref);

/**
 * Filter the choice record, keep only items accepted by the filter
 * function.  After the last document, the filter function is called
 * with document=nullptr.
 */
void
http_cache_choice_filter(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_filter_t callback,
                         void *callback_ctx,
                         struct async_operation_ref &async_ref);

/**
 * Clean up the choice record, removing expired items.
 */
void
http_cache_choice_cleanup(struct pool &pool, MemachedStock &stock,
                          const char *uri,
                          http_cache_choice_cleanup_t callback,
                          void *callback_ctx,
                          struct async_operation_ref &async_ref);

/**
 * Deletes the choice record.
 *
 * The data records are not deleted, but since no pointer exists
 * anymore, they are unused.  We could optimize later by deleting
 * those, too.
 */
void
http_cache_choice_delete(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         struct async_operation_ref &async_ref);

#endif
