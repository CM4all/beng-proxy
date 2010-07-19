/*
 * Load resources specified by a resource_address.  This library
 * integrates most of the other client libraries.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_LOADER_H
#define BENG_PROXY_RESOURCE_LOADER_H

#include "istream.h"
#include "http.h"

struct hstock;
struct resource_address;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

struct resource_loader *
resource_loader_new(pool_t pool, struct hstock *tcp_stock,
                    struct hstock *fcgi_stock, struct hstock *delegate_stock);

/**
 * Requests a resource.  This is a glue function which integrates all
 * client-side protocols implemented by beng-proxy.
 *
 * @param rl a resource_loader object created by resource_loader_new
 * @param address the address of the resource
 * @param status a HTTP status code for protocols which do have one
 */
void
resource_loader_request(struct resource_loader *rl, pool_t pool,
                        http_method_t method,
                        const struct resource_address *address,
                        http_status_t status, struct strmap *headers,
                        istream_t body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref);

#endif
