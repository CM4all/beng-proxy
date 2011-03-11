/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-request.h"
#include "http-response.h"
#include "header-writer.h"
#include "stock.h"
#include "async.h"
#include "ajp-client.h"
#include "uri-address.h"
#include "strmap.h"
#include "lease.h"
#include "tcp-stock.h"
#include "abort-close.h"

#include <inline/compiler.h>

#include <string.h>

struct ajp_request {
    pool_t pool;

    struct hstock *tcp_stock;
    struct stock_item *stock_item;

    const char *protocol;
    const char *remote_addr;
    const char *remote_host;
    const char *server_name;
    unsigned server_port;
    bool is_ssl;

    http_method_t method;
    const char *uri;
    struct strmap *headers;
    istream_t body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

/*
 * socket lease
 *
 */

static void
ajp_socket_release(bool reuse, void *ctx)
{
    struct ajp_request *hr = ctx;

    tcp_stock_put(hr->tcp_stock, hr->stock_item, !reuse);
}

static const struct lease ajp_socket_lease = {
    .release = ajp_socket_release,
};


/*
 * stock callback
 *
 */

static void
ajp_request_stock_ready(struct stock_item *item, void *ctx)
{
    struct ajp_request *hr = ctx;

    hr->stock_item = item;

    ajp_client_request(hr->pool,
                       tcp_stock_item_get(item),
                       tcp_stock_item_get_domain(item) == AF_LOCAL
                       ? ISTREAM_SOCKET : ISTREAM_TCP,
                       &ajp_socket_lease, hr,
                       hr->protocol, hr->remote_addr,
                       hr->remote_host, hr->server_name,
                       hr->server_port, hr->is_ssl,
                       hr->method, hr->uri, hr->headers, hr->body,
                       hr->handler.handler, hr->handler.ctx,
                       hr->async_ref);
}

static void
ajp_request_stock_error(GError *error, void *ctx)
{
    struct ajp_request *hr = ctx;

    http_response_handler_invoke_abort(&hr->handler, error);

    if (hr->body != NULL)
        istream_close_unused(hr->body);
}

static const struct stock_handler ajp_request_stock_handler = {
    .ready = ajp_request_stock_ready,
    .error = ajp_request_stock_error,
};


/*
 * constructor
 *
 */

void
ajp_stock_request(pool_t pool,
                  struct hstock *tcp_stock,
                  const char *protocol, const char *remote_addr,
                  const char *remote_host, const char *server_name,
                  unsigned server_port, bool is_ssl,
                  http_method_t method,
                  struct uri_with_address *uwa,
                  struct strmap *headers,
                  istream_t body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    struct ajp_request *hr;

    assert(uwa != NULL);
    assert(uwa->uri != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

    hr = p_malloc(pool, sizeof(*hr));
    hr->pool = pool;
    hr->tcp_stock = tcp_stock;
    hr->protocol = protocol;
    hr->remote_addr = remote_addr;
    hr->remote_host = remote_host;
    hr->server_name = server_name;
    hr->server_port = server_port;
    hr->is_ssl = is_ssl;
    hr->method = method;
    hr->uri = uwa->uri;

    hr->headers = headers;
    if (hr->headers == NULL)
        hr->headers = strmap_new(pool, 16);

    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    if (body != NULL) {
        hr->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, hr->body, async_ref);
    } else
        hr->body = NULL;

    tcp_stock_get(tcp_stock, pool,
                  uwa->uri, &uwa->addresses,
                  &ajp_request_stock_handler, hr,
                  async_ref);
}
