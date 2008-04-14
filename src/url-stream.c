/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "url-stream.h"
#include "http-response.h"
#include "header-writer.h"
#include "http-stock.h"
#include "stock.h"
#include "async.h"
#include "http-client.h"

#include <inline/compiler.h>

#include <string.h>

typedef struct url_stream *url_stream_t;

struct url_stream {
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
url_stream_stock_callback(void *ctx, struct stock_item *item)
{
    url_stream_t us = ctx;

    if (item == NULL) {
        http_response_handler_invoke_abort(&us->handler);

        if (us->body != NULL)
            istream_close(us->body);
    } else
        http_client_request(http_stock_item_get(item),
                            us->method, us->uri, us->headers, us->body,
                            us->handler.handler, us->handler.ctx,
                            us->async_ref);

    pool_unref(us->pool);
}


/*
 * constructor
 *
 */

void
url_stream_new(pool_t pool,
               struct hstock *http_client_stock,
               http_method_t method, const char *url,
               growing_buffer_t headers,
               istream_t body,
               const struct http_response_handler *handler,
               void *handler_ctx,
               struct async_operation_ref *async_ref)
{
    url_stream_t us;
    const char *host_and_port;

    assert(url != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

    pool_ref(pool);

    us = p_malloc(pool, sizeof(*us));
    us->pool = pool;
    us->method = method;

    us->headers = headers;
    if (us->headers == NULL)
        us->headers = growing_buffer_new(pool, 512);

    if (body == NULL)
        us->body = NULL;
    else
        /* XXX remove istream_hold(), it is only here because
           http-client.c resets istream->pool after the response is
           ready */
        us->body = istream_hold_new(pool, body);
    http_response_handler_set(&us->handler, handler, handler_ctx);
    us->async_ref = async_ref;

    if (memcmp(url, "http://", 7) == 0) {
        /* HTTP over TCP */
        const char *p, *slash;

        p = url + 7;
        slash = strchr(p, '/');
        if (slash == NULL || slash == p) {
            http_response_handler_invoke_abort(&us->handler);
            pool_unref(us->pool);
            return;
        }

        host_and_port = p_strndup(us->pool, p, slash - p);
        header_write(us->headers, "host", host_and_port);

        us->uri = slash;
    } else if (memcmp(url, "unix:/", 6) == 0) {
        /* HTTP over Unix socket */
        const char *p, *qmark;

        p = url + 5;
        us->uri = p;

        qmark = strchr(p, '?');
        if (qmark == NULL)
            host_and_port = p;
        else
            host_and_port = p_strndup(us->pool, p, qmark - p);
    } else {
        http_response_handler_invoke_abort(&us->handler);
        pool_unref(us->pool);
        return;
    }

    header_write(us->headers, "connection", "keep-alive");

    hstock_get(http_client_stock,
               host_and_port, NULL,
               url_stream_stock_callback, us,
               async_ref);
}
