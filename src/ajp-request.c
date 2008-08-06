/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-request.h"
#include "http-response.h"
#include "header-writer.h"
#include "ajp-stock.h"
#include "stock.h"
#include "async.h"
#include "ajp-client.h"
#include "uri-address.h"
#include "abort-unref.h"
#include "strmap.h"

#include <inline/compiler.h>

#include <string.h>

struct ajp_request {
    pool_t pool;

    http_method_t method;
    const char *uri;
    struct strmap *headers;
    istream_t body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


/*
 * stock callback
 *
 */

static void
ajp_request_stock_callback(void *ctx, struct stock_item *item)
{
    struct ajp_request *hr = ctx;

    if (item == NULL) {
        http_response_handler_invoke_abort(&hr->handler);

        if (hr->body != NULL)
            istream_close(hr->body);
    } else
        ajp_request(ajp_stock_item_get(item),
                    hr->pool,
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
ajp_stock_request(pool_t pool,
                  struct hstock *http_client_stock,
                  http_method_t method,
                  struct uri_with_address *uwa,
                  struct strmap *headers,
                  istream_t body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    struct ajp_request *hr;
    const char *host_and_port;

    assert(uwa != NULL);
    assert(uwa->uri != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

    hr = p_malloc(pool, sizeof(*hr));
    hr->pool = pool;
    hr->method = method;
    hr->uri = uwa->uri;

    hr->headers = headers;
    if (hr->headers == NULL)
        hr->headers = strmap_new(pool, 16);

    hr->body = body;

    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    pool_ref(pool);

    hstock_get(http_client_stock,
               host_and_port, uwa,
               ajp_request_stock_callback, hr,
               async_unref_on_abort(pool, async_ref));
}
