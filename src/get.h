/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GET_H
#define __BENG_GET_H

#include "istream.h"
#include "http.h"

struct http_cache;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
resource_get(struct http_cache *cache, pool_t pool,
             http_method_t method,
             const struct resource_address *address,
             struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif
