/*
 * High level AJP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_request.hxx"
#include "http_response.hxx"
#include "http_address.hxx"
#include "header_writer.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
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
#include "util/Cancellable.hxx"

#include <inline/compiler.h>

#include <string.h>
#include <sys/socket.h>

struct AjpRequest final : public StockGetHandler, Lease {
    struct pool &pool;
    EventLoop &event_loop;

    StockItem *stock_item;

    const char *const protocol;
    const char *const remote_addr;
    const char *const remote_host;
    const char *const server_name;
    const unsigned server_port;
    const bool is_ssl;

    const http_method_t method;
    const char *const uri;
    StringMap headers;
    Istream *body;

    HttpResponseHandler &handler;
    CancellablePointer &cancel_ptr;

    AjpRequest(struct pool &_pool, EventLoop &_event_loop,
               const char *_protocol, const char *_remote_addr,
               const char *_remote_host, const char *_server_name,
               unsigned _server_port, bool _is_ssl,
               http_method_t _method, const char *_uri,
               StringMap &&_headers,
               HttpResponseHandler &_handler,
               CancellablePointer &_cancel_ptr)
        :pool(_pool), event_loop(_event_loop),
         protocol(_protocol),
         remote_addr(_remote_addr), remote_host(_remote_host),
         server_name(_server_name), server_port(_server_port),
         is_ssl(_is_ssl),
         method(_method), uri(_uri),
         headers(std::move(_headers)),
         handler(_handler),
         cancel_ptr(_cancel_ptr) {
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
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

    ajp_client_request(pool, event_loop,
                       tcp_stock_item_get(item),
                       tcp_stock_item_get_domain(item) == AF_LOCAL
                       ? FdType::FD_SOCKET : FdType::FD_TCP,
                       *this,
                       protocol, remote_addr,
                       remote_host, server_name,
                       server_port, is_ssl,
                       method, uri, headers, body,
                       handler, cancel_ptr);
}

void
AjpRequest::OnStockItemError(GError *error)
{
    handler.InvokeError(error);

    if (body != nullptr)
        body->CloseUnused();
}

/*
 * constructor
 *
 */

void
ajp_stock_request(struct pool &pool, EventLoop &event_loop,
                  TcpBalancer &tcp_balancer,
                  unsigned session_sticky,
                  const char *protocol, const char *remote_addr,
                  const char *remote_host, const char *server_name,
                  unsigned server_port, bool is_ssl,
                  http_method_t method,
                  const HttpAddress &uwa,
                  StringMap &&headers,
                  Istream *body,
                  HttpResponseHandler &handler,
                  CancellablePointer &_cancel_ptr)
{
    assert(uwa.path != nullptr);
    assert(body == nullptr || !body->HasHandler());

    auto hr = NewFromPool<AjpRequest>(pool, pool, event_loop,
                                      protocol,
                                      remote_addr, remote_host,
                                      server_name, server_port,
                                      is_ssl, method, uwa.path,
                                      std::move(headers),
                                      handler, _cancel_ptr);

    auto *cancel_ptr = &_cancel_ptr;
    if (body != nullptr) {
        hr->body = istream_hold_new(pool, *body);
        cancel_ptr = &async_close_on_abort(pool, *hr->body, *cancel_ptr);
    } else
        hr->body = nullptr;

    tcp_balancer_get(tcp_balancer, pool,
                     false, SocketAddress::Null(),
                     session_sticky,
                     uwa.addresses,
                     20,
                     *hr, *cancel_ptr);
}
