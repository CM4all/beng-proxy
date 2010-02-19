/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-request.h"
#include "fcgi-stock.h"
#include "fcgi-client.h"
#include "http-response.h"
#include "socket-util.h"
#include "lease.h"
#include "tcp-stock.h"
#include "stock.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct fcgi_request {
    pool_t pool;

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
    struct strmap *headers;
    istream_t body;

    const char *const* params;
    unsigned num_params;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


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


static void
fcgi_stock_callback(void *ctx, struct stock_item *item)
{
    struct fcgi_request *request = ctx;

    if (item == NULL) {
        http_response_handler_invoke_abort(&request->handler);
        return;
    }

    request->stock_item = item;

    fcgi_client_request(request->pool, fcgi_stock_item_get(item),
                        &fcgi_socket_lease, request,
                        request->method, request->uri,
                        request->script_filename,
                        request->script_name, request->path_info,
                        request->query_string,
                        request->document_root,
                        request->headers, request->body,
                        request->params, request->num_params,
                        request->handler.handler, request->handler.ctx,
                        request->async_ref);
}


/*
 * constructor
 *
 */

void
fcgi_request(pool_t pool, struct hstock *fcgi_stock,
             const char *action,
             const char *path,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             struct strmap *headers, istream_t body,
             const char *const params[], unsigned num_params,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct fcgi_request *request;

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
    request->headers = headers;
    request->body = body != NULL
        ? istream_hold_new(pool, body)
        : NULL;
    request->params = params;
    request->num_params = num_params;

    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;

    fcgi_stock_get(fcgi_stock, pool,
                   action, NULL,
                   fcgi_stock_callback, request,
                   async_ref);
}
