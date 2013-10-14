/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_FILTER_CACHE_H
#define BENG_FILTER_CACHE_H

#include <http/status.h>

struct pool;
struct istream;
struct resource_loader;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct cache_stats;

#ifdef __cplusplus
extern "C" {
#endif

struct filter_cache *
filter_cache_new(struct pool *pool, size_t max_size,
                 struct resource_loader *resource_loader);

void
filter_cache_close(struct filter_cache *cache);

void
filter_cache_fork_cow(struct filter_cache *cache, bool inherit);

void
filter_cache_get_stats(const struct filter_cache *cache,
                       struct cache_stats *data);

void
filter_cache_flush(struct filter_cache *cache);

/**
 * @param source_id uniquely identifies the source; NULL means disable
 * the cache
 * @param status a HTTP status code for filter protocols which do have
 * one
 */
void
filter_cache_request(struct filter_cache *cache,
                     struct pool *pool,
                     const struct resource_address *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers,
                     struct istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
