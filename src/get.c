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

void
resource_get(struct http_cache *cache, pool_t pool,
             http_method_t method,
             const struct resource_address *address,
             struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct http_response_handler_ref handler_ref;

    assert(cache != NULL);
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        if (body != NULL)
            istream_close(body);

        static_file_get(pool, address->u.path,
                        handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_CGI:
        cgi_new(pool, address->u.cgi.jail,
                address->u.cgi.interpreter, address->u.cgi.action,
                address->u.cgi.path,
                method, "/" /* XXX */,
                "/" /* XXX */, "" /* XXX */,
                "" /* XXX */, "/var/www" /* XXX */,
                headers, body,
                handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_HTTP:
        http_cache_request(cache, pool,
                           method, address->u.http,
                           headers, body,
                           handler, handler_ctx, async_ref);
        return;
    }

    if (body != NULL)
        istream_close(body);

    http_response_handler_set(&handler_ref, handler, handler_ctx);
    http_response_handler_invoke_abort(&handler_ref);
}
