/*
 * High level "Local HTTP" client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_request.h"
#include "lhttp_stock.h"
#include "lhttp_address.h"
#include "http-response.h"
#include "http-client.h"
#include "stock.h"
#include "lease.h"
#include "istream.h"
#include "abort-close.h"

struct lhttp_request {
    struct pool *pool;

    struct hstock *lhttp_stock;
    struct stock_item *stock_item;

    const struct lhttp_address *address;

    http_method_t method;
    struct growing_buffer *headers;
    struct istream *body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

/*
 * socket lease
 *
 */

static void
lhttp_socket_release(bool reuse, void *ctx)
{
    struct lhttp_request *request = ctx;

    lhttp_stock_put(request->lhttp_stock, request->stock_item, !reuse);
}

static const struct lease lhttp_socket_lease = {
    .release = lhttp_socket_release,
};

/*
 * stock callback
 *
 */

static void
lhttp_stock_ready(struct stock_item *item, void *ctx)
{
    struct lhttp_request *request = ctx;

    request->stock_item = item;

    http_client_request(request->pool, lhttp_stock_item_get_socket(item),
                        lhttp_stock_item_get_type(item),
                        &lhttp_socket_lease, request,
                        request->method, request->address->uri,
                        request->headers, request->body, true,
                        request->handler.handler,
                        request->handler.ctx,
                        request->async_ref);
}

static void
lhttp_stock_error(GError *error, void *ctx)
{
    struct lhttp_request *request = ctx;

    http_response_handler_invoke_abort(&request->handler, error);

    if (request->body != NULL)
        istream_close_unused(request->body);
}

static const struct stock_get_handler lhttp_stock_handler = {
    .ready = lhttp_stock_ready,
    .error = lhttp_stock_error,
};

/*
 * constructor
 *
 */

void
lhttp_request(struct pool *pool, struct hstock *lhttp_stock,
              const struct lhttp_address *address,
              http_method_t method,
              struct growing_buffer *headers, struct istream *body,
              const struct http_response_handler *handler, void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    struct lhttp_request *request;

    GError *error = NULL;
    if (!jail_params_check(&address->jail, &error)) {
        if (body != NULL)
            istream_close(body);

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->lhttp_stock = lhttp_stock;
    request->address = address;
    request->method = method;
    request->headers = headers;

    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;

    if (body != NULL) {
        request->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, request->body, async_ref);
    } else
        request->body = NULL;

    lhttp_stock_get(lhttp_stock, pool, address,
                    &lhttp_stock_handler, request,
                    async_ref);
}
