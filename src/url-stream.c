/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "url-stream.h"
#include "client-socket.h"
#include "http-client.h"
#include "compiler.h"
#include "header-writer.h"
#include "url-stock.h"
#include "stock.h"
#include "async.h"

#include <daemon/log.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

struct url_stream {
    pool_t pool;

    http_method_t method;
    const char *uri;
    growing_buffer_t headers;
    off_t content_length;
    istream_t body;

    struct async_operation_ref async;
    struct stock_item *stock_item;

    struct http_response_handler_ref handler;
};

static void
url_stream_stock_callback(void *ctx, struct stock_item *item)
{
    url_stream_t us = ctx;

    assert(us->stock_item == NULL);

    async_ref_clear(&us->async);

    if (item == NULL) {
        http_response_handler_invoke_abort(&us->handler);
        pool_unref(us->pool);
        return;
    }

    us->stock_item = item;

    http_client_request(url_stock_item_get(item),
                        us->method, us->uri, us->headers,
                        us->content_length, us->body,
                        us->handler.handler, us->handler.ctx);

    if (us->body != NULL)
        istream_clear_unref(&us->body);

    pool_unref(us->pool);
}

url_stream_t attr_malloc
url_stream_new(pool_t pool,
               struct hstock *http_client_stock,
               http_method_t method, const char *url,
               growing_buffer_t headers,
               off_t content_length, istream_t body,
               const struct http_response_handler *handler,
               void *handler_ctx)
{
    url_stream_t us;
    const char *host_and_port;

    assert(url != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "url_stream", 4096);
#endif

    us = p_malloc(pool, sizeof(*us));
    us->pool = pool;
    us->method = method;

    us->headers = headers;
    if (us->headers == NULL)
        us->headers = growing_buffer_new(pool, 512);

    us->content_length = content_length;
    if (body == NULL)
        us->body = NULL;
    else
        /* XXX remove istream_hold(), it is only here because
           http-client.c resets istream->pool after the response is
           ready */
        istream_assign_ref(&us->body, istream_hold_new(pool, body));
    async_ref_clear(&us->async);
    us->stock_item = NULL;
    http_response_handler_set(&us->handler, handler, handler_ctx);

    if (memcmp(url, "http://", 7) == 0) {
        /* HTTP over TCP */
        const char *p, *slash;

        p = url + 7;
        slash = strchr(p, '/');
        if (slash == NULL || slash == p) {
            /* XXX */
            url_stream_close(us);
            return NULL;
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
        /* XXX */
        url_stream_close(us);
        return NULL;
    }

    header_write(us->headers, "connection", "keep-alive");

    hstock_get(http_client_stock, host_and_port,
               url_stream_stock_callback, us,
               &us->async);

    /* XXX check if "us" is still valid */
    return us;
}

void
url_stream_close(url_stream_t us)
{
    pool_t pool;

    assert(us != NULL);
    assert(us->pool != NULL);

    pool = us->pool;
    us->pool = NULL;

    if (us->body != NULL)
        istream_clear_unref(&us->body);

    if (async_ref_defined(&us->async)) {
        async_abort(&us->async);
        return;
    }

    assert(us->stock_item != NULL);

    http_client_connection_close(url_stock_item_get(us->stock_item));
    pool_unref(pool);
}
