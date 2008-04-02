/*
 * Caching HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_CACHE_H
#define __BENG_HTTP_CACHE_H

#include "istream.h"
#include "http.h"

struct http_cache;
struct hstock;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

struct http_cache *
http_cache_new(pool_t pool, size_t max_size,
               struct hstock *http_client_stock);

void
http_cache_close(struct http_cache *cache);

void
http_cache_request(struct http_cache *cache,
                   pool_t pool,
                   http_method_t method, const char *url,
                   struct strmap *headers, istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

#endif
