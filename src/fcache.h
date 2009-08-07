/*
 * Caching filter responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_FILTER_CACHE_H
#define BENG_FILTER_CACHE_H

#include "istream.h"
#include "http.h"

struct filter_cache;
struct hstock;
struct fcgi_stock;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

struct filter_cache *
filter_cache_new(pool_t pool, size_t max_size,
                 struct hstock *tcp_stock,
                 struct fcgi_stock *fcgi_stock);

void
filter_cache_close(struct filter_cache *cache);

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
                     pool_t pool,
                     const struct resource_address *address,
                     const char *source_id,
                     http_status_t status, struct strmap *headers, istream_t body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     struct async_operation_ref *async_ref);

#endif
