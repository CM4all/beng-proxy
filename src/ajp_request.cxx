/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "header_writer.hxx"
#include "stock.hxx"
#include "async.hxx"
#include "ajp_client.hxx"
#include "strmap.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "abort_close.hxx"
#include "istream.h"
#include "net/SocketAddress.hxx"
#include "pool.hxx"

#include <inline/compiler.h>

#include <string.h>
#include <sys/socket.h>

struct ajp_request {
    struct pool *pool;

    struct tcp_balancer *tcp_balancer;
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
    struct istream *body;

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
    struct ajp_request *hr = (struct ajp_request *)ctx;

    tcp_balancer_put(hr->tcp_balancer, hr->stock_item, !reuse);
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
    struct ajp_request *hr = (struct ajp_request *)ctx;

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
    struct ajp_request *hr = (struct ajp_request *)ctx;

    http_response_handler_invoke_abort(&hr->handler, error);

    if (hr->body != nullptr)
        istream_close_unused(hr->body);
}

static const struct stock_get_handler ajp_request_stock_handler = {
    .ready = ajp_request_stock_ready,
    .error = ajp_request_stock_error,
};


/*
 * constructor
 *
 */

void
ajp_stock_request(struct pool *pool,
                  struct tcp_balancer *tcp_balancer,
                  unsigned session_sticky,
                  const char *protocol, const char *remote_addr,
                  const char *remote_host, const char *server_name,
                  unsigned server_port, bool is_ssl,
                  http_method_t method,
                  const struct http_address *uwa,
                  struct strmap *headers,
                  struct istream *body,
                  const struct http_response_handler *handler,
                  void *handler_ctx,
                  struct async_operation_ref *async_ref)
{
    assert(uwa != nullptr);
    assert(uwa->path != nullptr);
    assert(handler != nullptr);
    assert(handler->response != nullptr);
    assert(body == nullptr || !istream_has_handler(body));

    auto hr = NewFromPool<struct ajp_request>(*pool);
    hr->pool = pool;
    hr->tcp_balancer = tcp_balancer;
    hr->protocol = protocol;
    hr->remote_addr = remote_addr;
    hr->remote_host = remote_host;
    hr->server_name = server_name;
    hr->server_port = server_port;
    hr->is_ssl = is_ssl;
    hr->method = method;

    hr->headers = headers;
    if (hr->headers == nullptr)
        hr->headers = strmap_new(pool);

    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    if (body != nullptr) {
        hr->body = istream_hold_new(pool, body);
        async_ref = &async_close_on_abort(*pool, *hr->body, *async_ref);
    } else
        hr->body = nullptr;

    hr->uri = uwa->path;

    tcp_balancer_get(tcp_balancer, pool,
                     false, SocketAddress::Null(),
                     session_sticky,
                     &uwa->addresses,
                     20,
                     &ajp_request_stock_handler, hr,
                     async_ref);
}
