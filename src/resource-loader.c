/*
 * Get resources, either a static file, from a CGI program or from a
 * HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "resource-loader.h"
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
#include "delegate-request.h"

#include <string.h>

struct resource_loader {
    struct hstock *tcp_stock;
    struct hstock *fcgi_stock;
    struct hstock *delegate_stock;
};

struct resource_loader *
resource_loader_new(pool_t pool, struct hstock *tcp_stock,
                    struct hstock *fcgi_stock, struct hstock *delegate_stock)
{
    assert(tcp_stock != NULL);
    assert(fcgi_stock != NULL);

    struct resource_loader *rl = p_malloc(pool, sizeof(*rl));

    rl->tcp_stock = tcp_stock;
    rl->fcgi_stock = fcgi_stock;
    rl->delegate_stock = delegate_stock;

    return rl;
}

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
resource_loader_request(struct resource_loader *rl, pool_t pool,
                        http_method_t method,
                        const struct resource_address *address,
                        http_status_t status, struct strmap *headers,
                        istream_t body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    assert(rl != NULL);
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
            if (rl->delegate_stock == NULL) {
                http_response_handler_direct_abort(handler, handler_ctx);
                return;
            }

            delegate_stock_request(rl->delegate_stock, pool,
                                   address->u.local.delegate,
                                   address->u.local.document_root,
                                   address->u.local.jail,
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
        fcgi_request(pool, rl->fcgi_stock, address->u.cgi.jail,
                     address->u.cgi.action,
                     address->u.cgi.path,
                     method, resource_address_cgi_uri(pool, address),
                     address->u.cgi.script_name,
                     address->u.cgi.path_info,
                     address->u.cgi.query_string,
                     address->u.cgi.document_root,
                     headers, body,
                     address->u.cgi.args, address->u.cgi.num_args,
                     handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_HTTP:
        http_request(pool, rl->tcp_stock,
                     method, address->u.http,
                     headers_dup(pool, headers), body,
                     handler, handler_ctx, async_ref);
        return;

    case RESOURCE_ADDRESS_AJP:
        ajp_stock_request(pool, rl->tcp_stock,
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
