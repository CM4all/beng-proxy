/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_FILTER_CACHE_HXX
#define BENG_FILTER_CACHE_HXX

#include <inline/compiler.h>
#include <http/status.h>

struct pool;
struct istream;
struct resource_loader;
struct ResourceAddress;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct AllocatorStats;

struct filter_cache *
filter_cache_new(struct pool *pool, size_t max_size,
                 struct resource_loader *resource_loader);

void
filter_cache_close(struct filter_cache *cache);

void
filter_cache_fork_cow(struct filter_cache *cache, bool inherit);

gcc_pure
AllocatorStats
filter_cache_get_stats(const struct filter_cache &cache);

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
                     const ResourceAddress *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers,
                     struct istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref);

#endif
