/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "get.h"
#include "resource-address.h"
#include "http-cache.h"
#include "http-request.h"
#include "http-response.h"
#include "static-file.h"
#include "cgi.h"
#include "fcgi-request.h"
#include "ajp-request.h"
#include "header-writer.h"
#include "pipe.h"
#include "delegate-get.h"

#include <string.h>

static const char *
extract_remote_host(const struct strmap *headers)
{
    const char *p = strmap_get_checked(headers, "via");
    if (p == NULL)
        return "";

    p = strrchr(p, ',');
    if (p == NULL)
        return "";

    p = strchr(p + 1, ' ');
    if (p == NULL)
        return "";

    return p + 1;
}

static const char *
extract_server_name(const struct strmap *headers)
{
    const char *p = strmap_get_checked(headers, "host");
    if (p == NULL)
        return ""; /* XXX */

    /* XXX remove port? */
    return p;
}

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
             struct async_operation_ref *async_ref)
{
    assert(tcp_stock != NULL);
    assert(fcgi_stock != NULL);
    assert(pool != NULL);
    assert(address != NULL);

    switch (address->type) {
    case RESOURCE_ADDRESS_NONE:
        break;

    case RESOURCE_ADDRESS_LOCAL:
        if (body != NULL)
            /* static files cannot receive a request body, close it */
            istream_close(body);

        if (address->u.local.delegate != NULL) {
            if (delegate_stock == NULL) {
                http_response_handler_direct_abort(handler, handler_ctx);
                return;
            }

            delegate_stock_get(delegate_stock, pool,
                               address->u.local.delegate,
                               address->u.local.path,
                               address->u.local.content_type,
                               handler, handler_ctx,
                               async_ref);
            return;
        }

        static_file_get(pool, address->u.local.path,
                        address->u.local.content_type,
                        handler, handler_ctx);
        return;

    case RESOURCE_ADDRESS_PIPE:
        pipe_filter(pool, address->u.cgi.path,
                    address->u.cgi.args, address->u.cgi.num_args,
                    status, headers, body,
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

    case RESOURCE_ADDRESS_FASTCGI:
        fcgi_request(pool, fcgi_stock, tcp_stock,
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
        if (cache != NULL)
            http_cache_request(cache, pool,
                               method, address->u.http,
                               headers, body,
                               handler, handler_ctx, async_ref);
        else
            /* no http_cache object passed - fall back to uncached
               HTTP */
            http_request(pool, tcp_stock,
                         method, address->u.http,
                         headers_dup(pool, headers), body,
                         handler, handler_ctx, async_ref);

        return;

    case RESOURCE_ADDRESS_AJP:
        ajp_stock_request(pool, tcp_stock,
                          "http", extract_remote_host(headers),
                          extract_remote_host(headers),
                          extract_server_name(headers),
                          80, /* XXX */
                          false,
                          method, address->u.http,
                          headers, body,
                          handler, handler_ctx, async_ref);
        return;
    }

    /* the resource could not be located, abort the request */

    if (body != NULL)
        istream_close(body);

    http_response_handler_direct_abort(handler, handler_ctx);
}
