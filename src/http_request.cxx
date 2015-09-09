/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_request.hxx"
#include "http_response.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "http_address.hxx"
#include "header_writer.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "stock.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "lease.hxx"
#include "abort_close.hxx"
#include "failure.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "filtered_socket.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

#include <inline/compiler.h>

#include <string.h>

struct HttpRequest final : public StockGetHandler {
    struct pool &pool;

    TcpBalancer &tcp_balancer;

    const unsigned session_sticky;

    const SocketFilter *const filter;
    SocketFilterFactory *const filter_factory;

    StockItem *stock_item;
    SocketAddress current_address;

    const http_method_t method;
    const struct http_address &address;
    HttpHeaders headers;
    struct istream *body;

    unsigned retries;

    struct http_response_handler_ref handler;
    struct async_operation_ref *const async_ref;

    HttpRequest(struct pool &_pool, TcpBalancer &_tcp_balancer,
                unsigned _session_sticky,
                const SocketFilter *_filter,
                SocketFilterFactory *_filter_factory,
                http_method_t _method,
                const struct http_address &_address,
                HttpHeaders &&_headers,
                const struct http_response_handler &_handler,
                void *_handler_ctx,
                struct async_operation_ref &_async_ref)
        :pool(_pool), tcp_balancer(_tcp_balancer),
         session_sticky(_session_sticky),
         filter(_filter), filter_factory(_filter_factory),
         method(_method), address(_address),
         headers(std::move(_headers)),
         async_ref(&_async_ref)
    {
        handler.Set(_handler, _handler_ctx);
    }

    void Dispose() {
        if (body != nullptr)
            istream_close_unused(body);
    }

    void Failed(GError *error) {
        Dispose();
        handler.InvokeAbort(error);
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;
};

/**
 * Is the specified error a server failure, that justifies
 * blacklisting the server for a while?
 */
static bool
is_server_failure(GError *error)
{
    return error->domain == http_client_quark() &&
        error->code != HTTP_CLIENT_UNSPECIFIED;
}

/*
 * HTTP response handler
 *
 */

static void
http_request_response_response(http_status_t status, struct strmap *headers,
                               struct istream *body, void *ctx)
{
    HttpRequest *hr = (HttpRequest *)ctx;

    failure_unset(hr->current_address, FAILURE_RESPONSE);

    hr->handler.InvokeResponse(status, headers, body);
}

static void
http_request_response_abort(GError *error, void *ctx)
{
    HttpRequest *hr = (HttpRequest *)ctx;

    if (hr->retries > 0 && hr->body == nullptr &&
        error->domain == http_client_quark() &&
        error->code == HTTP_CLIENT_REFUSED) {
        /* the server has closed the connection prematurely, maybe
           because it didn't want to get any further requests on that
           TCP connection.  Let's try again. */

        g_error_free(error);

        --hr->retries;
        tcp_balancer_get(hr->tcp_balancer, hr->pool,
                         false, SocketAddress::Null(),
                         hr->session_sticky,
                         hr->address.addresses,
                         30,
                         *hr, *hr->async_ref);
    } else {
        if (is_server_failure(error))
            failure_set(hr->current_address, FAILURE_RESPONSE, 20);

        hr->handler.InvokeAbort(error);
    }
}

static const struct http_response_handler http_request_response_handler = {
    .response = http_request_response_response,
    .abort = http_request_response_abort,
};


/*
 * socket lease
 *
 */

static void
http_socket_release(bool reuse, void *ctx)
{
    HttpRequest *hr = (HttpRequest *)ctx;

    tcp_balancer_put(hr->tcp_balancer, *hr->stock_item, !reuse);
}

static const struct lease http_socket_lease = {
    .release = http_socket_release,
};


/*
 * stock callback
 *
 */

void
HttpRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;
    current_address = tcp_balancer_get_last();

    void *filter_ctx = nullptr;
    if (filter_factory != nullptr) {
        GError *error = nullptr;
        filter_ctx = filter_factory->CreateFilter(&error);
        if (filter_ctx == nullptr) {
            Failed(error);
            return;
        }
    }

    http_client_request(pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        http_socket_lease, this,
                        tcp_stock_item_get_name(item),
                        filter, filter_ctx,
                        method, address.path, std::move(headers),
                        body, true,
                        http_request_response_handler, this,
                        *async_ref);
}

void
HttpRequest::OnStockItemError(GError *error)
{
    Failed(error);
}

/*
 * constructor
 *
 */

void
http_request(struct pool &pool,
             TcpBalancer &tcp_balancer,
             unsigned session_sticky,
             const SocketFilter *filter, SocketFilterFactory *filter_factory,
             http_method_t method,
             const struct http_address &uwa,
             HttpHeaders &&headers,
             struct istream *body,
             const struct http_response_handler &handler,
             void *handler_ctx,
             struct async_operation_ref &_async_ref)
{
    assert(uwa.host_and_port != nullptr);
    assert(uwa.path != nullptr);
    assert(handler.response != nullptr);
    assert(body == nullptr || !istream_has_handler(body));

    auto hr = NewFromPool<HttpRequest>(pool, pool, tcp_balancer,
                                       session_sticky, filter, filter_factory,
                                       method, uwa, std::move(headers),
                                       handler, handler_ctx,
                                       _async_ref);

    struct async_operation_ref *async_ref = &_async_ref;
    if (body != nullptr) {
        body = istream_hold_new(&pool, body);
        async_ref = &async_close_on_abort(pool, *body, *async_ref);
    }

    hr->body = body;

    GrowingBuffer &headers2 = hr->headers.MakeBuffer(pool, 256);
    if (uwa.host_and_port != nullptr)
        header_write(&headers2, "host", uwa.host_and_port);

    header_write(&headers2, "connection", "keep-alive");

    hr->retries = 2;
    tcp_balancer_get(tcp_balancer, pool,
                     false, SocketAddress::Null(),
                     session_sticky,
                     uwa.addresses,
                     30,
                     *hr, *async_ref);
}
