/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "get.h"
#include "resource-address.h"
#include "resource-loader.h"
#include "http-cache.h"
#include "http-request.h"
#include "http-response.h"
#include "static-file.h"
#include "cgi.h"
#include "fcgi-request.h"
#include "ajp-request.h"
#include "header-writer.h"
#include "pipe.h"
#include "delegate-request.h"

#include <string.h>

void
resource_get(struct http_cache *cache,
             struct hstock *tcp_stock,
             struct hstock *fcgi_stock,
             struct hstock *delegate_stock,
             pool_t pool,
             http_method_t method,
             const struct resource_address *address,
             http_status_t status, struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    assert(tcp_stock != NULL);
    assert(fcgi_stock != NULL);
    assert(pool != NULL);
    assert(address != NULL);

    if (cache != NULL) {
        http_cache_request(cache, pool,
                           method, address,
                           headers, body,
                           handler, handler_ctx, async_ref);
    } else {
        struct resource_loader *rl =
            resource_loader_new(pool, tcp_stock, fcgi_stock, delegate_stock);
        resource_loader_request(rl, pool,
                                method, address, status, headers, body,
                                handler, handler_ctx, async_ref);
    }
}
