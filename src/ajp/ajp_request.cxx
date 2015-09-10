/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "header_writer.hxx"
#include "stock/Stock.hxx"
#include "stock/GetHandler.hxx"
#include "async.hxx"
#include "ajp_client.hxx"
#include "strmap.hxx"
#include "lease.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "abort_close.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "net/SocketAddress.hxx"
#include "pool.hxx"

#include <inline/compiler.h>

#include <string.h>
#include <sys/socket.h>

struct AjpRequest final : public StockGetHandler, Lease {
    struct pool *pool;

    TcpBalancer *tcp_balancer;
    StockItem *stock_item;

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

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        tcp_balancer_put(*tcp_balancer, *stock_item, !reuse);
    }
};

/*
 * stock callback
 *
 */

void
AjpRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;

    ajp_client_request(pool,
                       tcp_stock_item_get(item),
                       tcp_stock_item_get_domain(item) == AF_LOCAL
                       ? FdType::FD_SOCKET : FdType::FD_TCP,
                       *this,
                       protocol, remote_addr,
                       remote_host, server_name,
                       server_port, is_ssl,
                       method, uri, headers, body,
                       handler.handler, handler.ctx,
                       async_ref);
}

void
AjpRequest::OnStockItemError(GError *error)
{
    handler.InvokeAbort(error);

    if (body != nullptr)
        istream_close_unused(body);
}

/*
 * constructor
 *
 */

void
ajp_stock_request(struct pool *pool,
                  TcpBalancer *tcp_balancer,
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

    auto hr = NewFromPool<AjpRequest>(*pool);
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

    hr->handler.Set(*handler, handler_ctx);
    hr->async_ref = async_ref;

    if (body != nullptr) {
        hr->body = istream_hold_new(pool, body);
        async_ref = &async_close_on_abort(*pool, *hr->body, *async_ref);
    } else
        hr->body = nullptr;

    hr->uri = uwa->path;

    tcp_balancer_get(*tcp_balancer, *pool,
                     false, SocketAddress::Null(),
                     session_sticky,
                     uwa->addresses,
                     20,
                     *hr, *async_ref);
}
