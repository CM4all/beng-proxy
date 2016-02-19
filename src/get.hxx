/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_GET_HXX
#define BENG_PROXY_GET_HXX

#include <http/method.h>
#include <http/status.h>

struct pool;
class Istream;
class HttpCache;
struct ResourceAddress;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

/**
 * Requests a resource.  This is a glue function which integrates all
 * client-side protocols implemented by beng-proxy.
 *
 * @param cache a HTTP cache object (optional)
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 * @param address the address of the resource
 */
void
resource_get(HttpCache *cache,
             struct pool *pool,
             unsigned session_sticky,
             http_method_t method,
             const ResourceAddress *address,
             struct strmap *headers,
             Istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref);

#endif
