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
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "lease.hxx"
#include "failure.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "filtered_socket.hxx"
#include "pool.hxx"
#include "GException.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

#include <inline/compiler.h>

#include <string.h>

class HttpRequest final
    : Cancellable, StockGetHandler, Lease, HttpResponseHandler {

    struct pool &pool;
    EventLoop &event_loop;

    TcpBalancer &tcp_balancer;

    const sticky_hash_t session_sticky;

    const SocketFilter *const filter;
    SocketFilterFactory *const filter_factory;

    StockItem *stock_item;

    const http_method_t method;
    const HttpAddress &address;
    HttpHeaders headers;
    UnusedHoldIstreamPtr body;

    unsigned retries;

    HttpResponseHandler &handler;
    CancellablePointer cancel_ptr;

    bool response_sent = false, reuse;

    enum class LeaseState : uint8_t {
        NONE,
        BUSY,
        PENDING,
    } lease_state = LeaseState::NONE;

public:
    HttpRequest(struct pool &_pool, EventLoop &_event_loop,
                TcpBalancer &_tcp_balancer,
                sticky_hash_t _session_sticky,
                const SocketFilter *_filter,
                SocketFilterFactory *_filter_factory,
                http_method_t _method,
                const HttpAddress &_address,
                HttpHeaders &&_headers,
                Istream *_body,
                HttpResponseHandler &_handler,
                CancellablePointer &_cancel_ptr)
        :pool(_pool), event_loop(_event_loop), tcp_balancer(_tcp_balancer),
         session_sticky(_session_sticky),
         filter(_filter), filter_factory(_filter_factory),
         method(_method), address(_address),
         headers(std::move(_headers)), body(pool, _body),
         /* can only retry if there is no request body */
         retries(_body != nullptr ? 2 : 0),
         handler(_handler)
    {
        _cancel_ptr = *this;

        if (address.host_and_port != nullptr)
            headers.Write("host", address.host_and_port);

        headers.Write("connection", "keep-alive");
    }

    void BeginConnect() {
        tcp_balancer_get(tcp_balancer, pool,
                         false, SocketAddress::Null(),
                         session_sticky,
                         address.addresses,
                         30,
                         *this, cancel_ptr);
    }

private:
    void Destroy() {
        assert(lease_state == LeaseState::NONE);

        DeleteFromPool(pool, this);
    }

    void DoRelease() {
        assert(lease_state == LeaseState::PENDING);

        lease_state = LeaseState::NONE;
        stock_item->Put(!reuse);
    }

    bool CheckRelease() {
        if (lease_state == LeaseState::PENDING)
            DoRelease();
        return lease_state == LeaseState::NONE;
    }

    void ResponseSent() {
        assert(!response_sent);
        response_sent = true;

        if (CheckRelease())
            Destroy();
    }

    void Failed(GError *error) {
        body.Clear();
        handler.InvokeError(error);
        ResponseSent();
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        assert(!response_sent);

        /* this pool reference is necessary because
           cancel_ptr.Cancel() may release the only remaining
           reference on the pool */
        const ScopePoolRef ref(pool TRACE_ARGS);

        body.Clear();
        cancel_ptr.Cancel();

        CheckRelease();
        Destroy();
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
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

void
HttpRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
                            Istream *_body)
{
    assert(lease_state != LeaseState::NONE);
    assert(!response_sent);

    failure_unset(tcp_stock_item_get_address(*stock_item), FAILURE_RESPONSE);

    handler.InvokeResponse(status, std::move(_headers), _body);
    ResponseSent();
}

void
HttpRequest::OnHttpError(GError *error)
{
    assert(lease_state != LeaseState::NONE);
    assert(!response_sent);

    if (retries > 0 &&
        error->domain == http_client_quark() &&
        error->code == HTTP_CLIENT_REFUSED) {
        /* the server has closed the connection prematurely, maybe
           because it didn't want to get any further requests on that
           TCP connection.  Let's try again. */

        g_error_free(error);

        --retries;
        BeginConnect();
    } else {
        if (is_server_failure(error))
            failure_set(tcp_stock_item_get_address(*stock_item),
                        FAILURE_RESPONSE,
                        std::chrono::seconds(20));

        Failed(error);
    }
}

/*
 * stock callback
 *
 */

void
HttpRequest::OnStockItemReady(StockItem &item)
{
    assert(lease_state == LeaseState::NONE);
    assert(!response_sent);

    stock_item = &item;
    lease_state = LeaseState::BUSY;

    void *filter_ctx = nullptr;
    if (filter_factory != nullptr) {
        try {
            filter_ctx = filter_factory->CreateFilter();
        } catch (const std::runtime_error &e) {
            item.Put(false);
            Failed(ToGError(e));
            return;
        }
    }

    http_client_request(pool, event_loop,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *this,
                        item.GetStockName(),
                        filter, filter_ctx,
                        method, address.path, std::move(headers),
                        body.Steal(), true,
                        *this, cancel_ptr);
}

void
HttpRequest::OnStockItemError(GError *error)
{
    assert(lease_state == LeaseState::NONE);
    assert(!response_sent);

    Failed(error);
}

/*
 * Lease
 *
 */

void
HttpRequest::ReleaseLease(bool _reuse)
{
    assert(lease_state == LeaseState::BUSY);

    lease_state = LeaseState::PENDING;
    reuse = _reuse;

    if (response_sent) {
        DoRelease();
        Destroy();
    }
}

/*
 * constructor
 *
 */

void
http_request(struct pool &pool, EventLoop &event_loop,
             TcpBalancer &tcp_balancer,
             sticky_hash_t session_sticky,
             const SocketFilter *filter, SocketFilterFactory *filter_factory,
             http_method_t method,
             const HttpAddress &uwa,
             HttpHeaders &&headers,
             Istream *body,
             HttpResponseHandler &handler,
             CancellablePointer &_cancel_ptr)
{
    assert(uwa.host_and_port != nullptr);
    assert(uwa.path != nullptr);
    assert(body == nullptr || !body->HasHandler());

    auto hr = NewFromPool<HttpRequest>(pool, pool, event_loop, tcp_balancer,
                                       session_sticky, filter, filter_factory,
                                       method, uwa, std::move(headers), body,
                                       handler, _cancel_ptr);

    hr->BeginConnect();
}
