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
class Istream;
struct resource_loader;
struct ResourceAddress;
struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct AllocatorStats;
class FilterCache;

FilterCache *
filter_cache_new(struct pool *pool, size_t max_size,
                 struct resource_loader *resource_loader);

void
filter_cache_close(FilterCache *cache);

void
filter_cache_fork_cow(FilterCache *cache, bool inherit);

gcc_pure
AllocatorStats
filter_cache_get_stats(const FilterCache &cache);

void
filter_cache_flush(FilterCache *cache);

/**
 * @param source_id uniquely identifies the source; NULL means disable
 * the cache
 * @param status a HTTP status code for filter protocols which do have
 * one
 */
void
filter_cache_request(FilterCache *cache,
                     struct pool *pool,
                     const ResourceAddress *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers,
                     Istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref);

#endif
