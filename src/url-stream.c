/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "url-stream.h"
#include "compiler.h"
#include "header-writer.h"
#include "url-stock.h"
#include "stock.h"
#include "async.h"

#include <string.h>

typedef struct url_stream *url_stream_t;

struct url_stream {
    pool_t pool;

    http_method_t method;
    const char *uri;
    growing_buffer_t headers;
    off_t content_length;
    istream_t body;

    struct async_operation async;

    struct async_operation_ref stock_get_operation;
    struct stock_item *stock_item;

    struct http_response_handler_ref handler;
};


/*
 * http response handler
 *
 */

static void 
url_stream_response(http_status_t status, strmap_t headers,
                    off_t content_length, istream_t body,
                    void *ctx)
{
    struct url_stream *us = ctx;

    http_response_handler_invoke_response(&us->handler, status, headers,
                                          content_length, body);

    pool_unref(us->pool);
}

static void 
url_stream_response_abort(void *ctx)
{
    struct url_stream *us = ctx;

    http_response_handler_invoke_abort(&us->handler);

    pool_unref(us->pool);
}

static const struct http_response_handler url_stream_response_handler = {
    .response = url_stream_response,
    .abort = url_stream_response_abort,
};


/*
 * stock callback
 *
 */

static void
url_stream_stock_callback(void *ctx, struct stock_item *item)
{
    url_stream_t us = ctx;

    assert(us->stock_item == NULL);

    async_ref_clear(&us->stock_get_operation);

    if (item == NULL) {
        http_response_handler_invoke_abort(&us->handler);
        pool_unref(us->pool);
        return;
    }

    us->stock_item = item;

    http_client_request(url_stock_item_get(item),
                        us->method, us->uri, us->headers,
                        us->content_length, us->body,
                        &url_stream_response_handler, us);

    if (us->body != NULL)
        istream_clear_unref(&us->body);
}


/*
 * async operation
 *
 */

static struct url_stream *
async_to_url_stream(struct async_operation *ao)
{
    return (struct url_stream*)(((char*)ao) - offsetof(struct url_stream, async));
}

static void
url_stream_abort(struct async_operation *ao)
{
    struct url_stream *us = async_to_url_stream(ao);

    assert(us != NULL);

    if (us->body != NULL)
        istream_clear_unref(&us->body);

    if (async_ref_defined(&us->stock_get_operation)) {
        async_abort(&us->stock_get_operation);
        return;
    }

    assert(us->stock_item != NULL);

    http_client_connection_close(url_stock_item_get(us->stock_item));
    pool_unref(us->pool);
}

static struct async_operation_class url_stream_async_operation = {
    .abort = url_stream_abort,
};


/*
 * constructor
 *
 */

void
url_stream_new(pool_t pool,
               struct hstock *http_client_stock,
               http_method_t method, const char *url,
               growing_buffer_t headers,
               off_t content_length, istream_t body,
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
    async_ref_clear(&us->stock_get_operation);
    us->stock_item = NULL;
    http_response_handler_set(&us->handler, handler, handler_ctx);

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

    async_init(&us->async, &url_stream_async_operation);
    async_ref_set(async_ref, &us->async);

    hstock_get(http_client_stock, host_and_port,
               url_stream_stock_callback, us,
               &us->stock_get_operation);
}
