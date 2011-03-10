/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-request.h"
#include "http-response.h"
#include "header-writer.h"
#include "tcp-stock.h"
#include "stock.h"
#include "async.h"
#include "http-client.h"
#include "uri-address.h"
#include "growing-buffer.h"
#include "lease.h"
#include "abort-close.h"

#include <inline/compiler.h>

#include <string.h>

struct http_request {
    pool_t pool;

    struct hstock *tcp_stock;
    const char *host_and_port;
    struct stock_item *stock_item;

    http_method_t method;
    const char *uri;
    struct growing_buffer *headers;
    istream_t body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

static GQuark
http_request_quark(void)
{
    return g_quark_from_static_string("http_request");
}


/*
 * socket lease
 *
 */

static void
http_socket_release(bool reuse, void *ctx)
{
    struct http_request *hr = ctx;

    hstock_put(hr->tcp_stock, hr->host_and_port, hr->stock_item, !reuse);
}

static const struct lease http_socket_lease = {
    .release = http_socket_release,
};


/*
 * stock callback
 *
 */

static void
http_request_stock_ready(struct stock_item *item, void *ctx)
{
    struct http_request *hr = ctx;

    hr->stock_item = item;

    http_client_request(hr->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &http_socket_lease, hr,
                        hr->method, hr->uri, hr->headers, hr->body,
                        hr->handler.handler, hr->handler.ctx,
                        hr->async_ref);
}

static void
http_request_stock_error(GError *error, void *ctx)
{
    struct http_request *hr = ctx;

    http_response_handler_invoke_abort(&hr->handler, error);

    if (hr->body != NULL)
        istream_close_unused(hr->body);
}

static const struct stock_handler http_request_stock_handler = {
    .ready = http_request_stock_ready,
    .error = http_request_stock_error,
};


/*
 * constructor
 *
 */

void
http_request(pool_t pool,
             struct hstock *tcp_stock,
             http_method_t method,
             struct uri_with_address *uwa,
             struct growing_buffer *headers,
             istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct http_request *hr;
    const char *host_and_port;

    assert(uwa != NULL);
    assert(uwa->uri != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

    hr = p_malloc(pool, sizeof(*hr));
    hr->pool = pool;
    hr->tcp_stock = tcp_stock;
    hr->method = method;

    hr->headers = headers;
    if (hr->headers == NULL)
        hr->headers = growing_buffer_new(pool, 512);

    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    if (body != NULL) {
        hr->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, hr->body, async_ref);
    } else
        hr->body = NULL;

    if (memcmp(uwa->uri, "http://", 7) == 0) {
        /* HTTP over TCP */
        const char *p, *slash;

        p = uwa->uri + 7;
        slash = strchr(p, '/');
        if (slash == p) {
            GError *error =
                g_error_new_literal(http_request_quark(), 0,
                                    "malformed HTTP URI");
            http_response_handler_invoke_abort(&hr->handler, error);
            return;
        }

        if (slash == NULL) {
            host_and_port = p;
            slash = "/";
        } else
            host_and_port = p_strndup(hr->pool, p, slash - p);

        header_write(hr->headers, "host", host_and_port);

        hr->uri = slash;
    } else if (memcmp(uwa->uri, "unix:/", 6) == 0) {
        /* HTTP over Unix socket */
        const char *p, *qmark;

        p = uwa->uri + 5;
        hr->uri = p;

        qmark = strchr(p, '?');
        if (qmark == NULL)
            host_and_port = p;
        else
            host_and_port = p_strndup(hr->pool, p, qmark - p);
    } else {
        GError *error =
            g_error_new_literal(http_request_quark(), 0,
                                "malformed URI");

        http_response_handler_invoke_abort(&hr->handler, error);
        return;
    }

    header_write(hr->headers, "connection", "keep-alive");

    hr->host_and_port = host_and_port;
    tcp_stock_get(tcp_stock, pool,
                  host_and_port, uwa,
                  &http_request_stock_handler, hr,
                  async_ref);
}
