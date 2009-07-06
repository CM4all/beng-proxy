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
struct fcgi_stock;
struct hstock;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

/**
 * Requests a resource.  This is a glue function which integrates all
 * client-side protocols implemented by beng-proxy.
 *
 * @param cache a HTTP cache object (optional)
 * @param tcp_stock the stock (pool) for TCP client connections
 * @param fcgi_stock the stock for FastCGI instances
 * @param delegate_stock the stock for delegate programs
 * @param address the address of the resource
 * @param status a HTTP status code for protocols which do have one
 */
void
resource_get(struct http_cache *cache,
             struct hstock *tcp_stock,
             struct fcgi_stock *fcgi_stock,
             struct hstock *delegate_stock,
             pool_t pool,
             http_method_t method,
             const struct resource_address *address,
             http_status_t status, struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif
