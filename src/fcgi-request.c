/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-request.h"
#include "fcgi-stock.h"
#include "fcgi-client.h"
#include "http-response.h"
#include "lease.h"
#include "tcp-stock.h"
#include "stock.h"
#include "abort-close.h"
#include "jail.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct fcgi_request {
    struct pool *pool;

    struct hstock *fcgi_stock;
    const char *action;
    struct stock_item *stock_item;

    http_method_t method;
    const char *uri;
    const char *script_filename;
    const char *script_name;
    const char *path_info;
    const char *query_string;
    const char *document_root;
    const char *remote_addr;
    struct strmap *headers;
    struct istream *body;

    const char *const* params;
    unsigned num_params;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

static GQuark
fcgi_request_quark(void)
{
    return g_quark_from_static_string("fcgi_request");
}


/*
 * socket lease
 *
 */

static void
fcgi_socket_release(bool reuse, void *ctx)
{
    struct fcgi_request *request = ctx;

    fcgi_stock_put(request->fcgi_stock, request->stock_item, !reuse);
}

static const struct lease fcgi_socket_lease = {
    .release = fcgi_socket_release,
};


/*
 * stock callback
 *
 */

static void
fcgi_stock_ready(struct stock_item *item, void *ctx)
{
    struct fcgi_request *request = ctx;

    request->stock_item = item;

    const char *script_filename =
        fcgi_stock_translate_path(item, request->script_filename,
                                  request->pool);
    const char *document_root =
        fcgi_stock_translate_path(item, request->document_root, request->pool);

    fcgi_client_request(request->pool, fcgi_stock_item_get(item),
                        fcgi_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &fcgi_socket_lease, request,
                        request->method, request->uri,
                        script_filename,
                        request->script_name, request->path_info,
                        request->query_string,
                        document_root,
                        request->remote_addr,
                        request->headers, request->body,
                        request->params, request->num_params,
                        request->handler.handler, request->handler.ctx,
                        request->async_ref);
}

static void
fcgi_stock_error(GError *error, void *ctx)
{
    struct fcgi_request *request = ctx;

    http_response_handler_invoke_abort(&request->handler, error);
}

static const struct stock_handler fcgi_stock_handler = {
    .ready = fcgi_stock_ready,
    .error = fcgi_stock_error,
};


/*
 * constructor
 *
 */

void
fcgi_request(struct pool *pool, struct hstock *fcgi_stock,
             const struct jail_params *jail,
             const char *action,
             const char *path,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             const char *remote_addr,
             struct strmap *headers, struct istream *body,
             const char *const params[], unsigned num_params,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct fcgi_request *request;

    if (jail != NULL && jail->enabled && jail->home_directory == NULL) {
        GError *error =
            g_error_new_literal(fcgi_request_quark(), 0,
                                "No document root");
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    if (action == NULL)
        action = path;

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->fcgi_stock = fcgi_stock;
    request->action = action;
    request->method = method;
    request->uri = uri;
    request->script_filename = path;
    request->script_name = script_name;
    request->path_info = path_info;
    request->query_string = query_string;
    request->document_root = document_root;
    request->remote_addr = remote_addr;
    request->headers = headers;
    request->params = params;
    request->num_params = num_params;

    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;

    if (body != NULL) {
        request->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, request->body, async_ref);
    } else
        request->body = NULL;

    fcgi_stock_get(fcgi_stock, pool, jail,
                   action,
                   &fcgi_stock_handler, request,
                   async_ref);
}
