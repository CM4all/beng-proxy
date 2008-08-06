/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "get.h"
#include "resource-address.h"
#include "http-cache.h"
#include "http-response.h"
#include "static-file.h"
#include "cgi.h"
#include "ajp-request.h"

void
resource_get(struct http_cache *cache,
             struct hstock *ajp_client_stock,
             pool_t pool,
             http_method_t method,
             const struct resource_address *address,
             struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    assert(cache != NULL);
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        if (body != NULL)
            istream_close(body);

        static_file_get(pool, address->u.local.path,
                        handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_CGI:
        cgi_new(pool, address->u.cgi.jail,
                address->u.cgi.interpreter, address->u.cgi.action,
                address->u.cgi.path,
                method, resource_address_cgi_uri(pool, address),
                address->u.cgi.script_name,
                address->u.cgi.path_info,
                address->u.cgi.query_string,
                address->u.cgi.document_root,
                headers, body,
                handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_HTTP:
        http_cache_request(cache, pool,
                           method, address->u.http,
                           headers, body,
                           handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_AJP:
        ajp_stock_request(pool, ajp_client_stock,
                          method, address->u.http,
                          headers, body,
                          handler, handler_ctx, async_ref);
        return;
    }

    if (body != NULL)
        istream_close(body);

    http_response_handler_direct_abort(handler, handler_ctx);
}
