/*
 * Load resources specified by a resource_address.  This library
 * integrates most of the other client libraries.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_LOADER_HXX
#define BENG_PROXY_RESOURCE_LOADER_HXX

#include <http/method.h>
#include <http/status.h>

struct pool;
class Istream;
struct StockMap;
struct LhttpStock;
struct FcgiStock;
struct NfsCache;
struct TcpBalancer;
struct ResourceAddress;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

struct resource_loader *
resource_loader_new(struct pool *pool, TcpBalancer *tcp_balancer,
                    LhttpStock *lhttp_stock,
                    FcgiStock *fcgi_stock, StockMap *was_stock,
                    StockMap *delegate_stock,
                    NfsCache *nfs_cache);

/**
 * Requests a resource.  This is a glue function which integrates all
 * client-side protocols implemented by beng-proxy.
 *
 * @param rl a resource_loader object created by resource_loader_new
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 * @param address the address of the resource
 * @param status a HTTP status code for protocols which do have one
 */
void
resource_loader_request(struct resource_loader *rl, struct pool *pool,
                        unsigned session_sticky,
                        http_method_t method,
                        const ResourceAddress *address,
                        http_status_t status, struct strmap *headers,
                        Istream *body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref);

#endif
