/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-request.h"
#include "http-response.h"
#include "header-writer.h"
#include "http-stock.h"
#include "stock.h"
#include "async.h"
#include "http-client.h"
#include "uri-address.h"

#include <inline/compiler.h>

#include <string.h>

struct http_request {
    pool_t pool;

    http_method_t method;
    const char *uri;
    growing_buffer_t headers;
    istream_t body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


/*
 * stock callback
 *
 */

static void
http_request_stock_callback(void *ctx, struct stock_item *item)
{
    struct http_request *hr = ctx;

    if (item == NULL) {
        http_response_handler_invoke_abort(&hr->handler);

        if (hr->body != NULL)
            istream_close(hr->body);
    } else
        http_client_request(http_stock_item_get(item),
                            hr->method, hr->uri, hr->headers, hr->body,
                            hr->handler.handler, hr->handler.ctx,
                            hr->async_ref);

    pool_unref(hr->pool);
}


/*
 * constructor
 *
 */

void
http_request(pool_t pool,
             struct hstock *http_client_stock,
             http_method_t method,
             struct uri_with_address *uwa,
             growing_buffer_t headers,
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

    pool_ref(pool);

    hr = p_malloc(pool, sizeof(*hr));
    hr->pool = pool;
    hr->method = method;

    hr->headers = headers;
    if (hr->headers == NULL)
        hr->headers = growing_buffer_new(pool, 512);

    if (body == NULL)
        hr->body = NULL;
    else
        /* XXX remove istream_hold(), it is only here because
           http-client.c resets istream->pool after the response is
           ready */
        hr->body = istream_hold_new(pool, body);
    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    if (memcmp(uwa->uri, "http://", 7) == 0) {
        /* HTTP over TCP */
        const char *p, *slash;

        p = uwa->uri + 7;
        slash = strchr(p, '/');
        if (slash == NULL || slash == p) {
            http_response_handler_invoke_abort(&hr->handler);
            pool_unref(hr->pool);
            return;
        }

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
        http_response_handler_invoke_abort(&hr->handler);
        pool_unref(hr->pool);
        return;
    }

    header_write(hr->headers, "connection", "keep-alive");

    hstock_get(http_client_stock,
               host_and_port, uwa,
               http_request_stock_callback, hr,
               async_ref);
}
