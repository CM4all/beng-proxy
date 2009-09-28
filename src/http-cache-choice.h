/*
 * Caching HTTP responses.  Memcached indirect backend.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CACHE_CHOICE_H
#define BENG_PROXY_HTTP_CACHE_CHOICE_H

#include "pool.h"

struct http_cache_choice;
struct http_cache_info;
struct strmap;
struct memcached_stock;
struct async_operation_ref;

typedef void (*http_cache_choice_get_t)(const char *key, bool unclean, void *ctx);
typedef void (*http_cache_choice_commit_t)(void *ctx);
typedef void (*http_cache_choice_cleanup_t)(void *ctx);
typedef void (*http_cache_choice_delete_t)(void *ctx);

const char *
http_cache_choice_vary_key(pool_t pool, const char *uri, struct strmap *vary);

void
http_cache_choice_get(pool_t pool, struct memcached_stock *stock,
                      const char *uri, const struct strmap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      struct async_operation_ref *async_ref);

struct http_cache_choice *
http_cache_choice_prepare(pool_t pool, const char *uri,
                          const struct http_cache_info *info,
                          struct strmap *vary);

void
http_cache_choice_commit(struct http_cache_choice *choice,
                         struct memcached_stock *stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref);

/**
 * Clean up the choice record, removing expired items.
 */
void
http_cache_choice_cleanup(pool_t pool, struct memcached_stock *stock,
                          const char *uri,
                          http_cache_choice_cleanup_t callback,
                          void *callback_ctx,
                          struct async_operation_ref *async_ref);

/**
 * Deletes the choice record.
 *
 * The data records are not deleted, but since no pointer exists
 * anymore, they are unused.  We could optimize later by deleting
 * those, too.
 */
void
http_cache_choice_delete(pool_t pool, struct memcached_stock *stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         struct async_operation_ref *async_ref);

#endif
