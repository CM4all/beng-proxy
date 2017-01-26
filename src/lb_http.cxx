/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_http.hxx"
#include "lb_instance.hxx"
#include "lb_connection.hxx"
#include "lb_config.hxx"
#include "lb_session.hxx"
#include "lb_cookie.hxx"
#include "lb_jvm_route.hxx"
#include "lb_headers.hxx"
#include "lb_log.hxx"
#include "ssl/ssl_filter.hxx"
#include "address_sticky.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_client.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "header_writer.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "stock/Lease.hxx"
#include "access_log.hxx"
#include "strmap.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "gerrno.h"
#include "util/Cancellable.hxx"

#include <http/status.h>
#include <daemon/log.h>

struct LbRequest final
    : Cancellable, StockGetHandler, HttpResponseHandler {

    LbConnection &connection;
    const LbClusterConfig *cluster;

    TcpBalancer &balancer;

    HttpServerRequest &request;

    /**
     * The request body.
     */
    UnusedHoldIstreamPtr body;

    CancellablePointer cancel_ptr;

    StockItem *stock_item;

    unsigned new_cookie = 0;

    LbRequest(LbConnection &_connection, TcpBalancer &_balancer,
              HttpServerRequest &_request,
              CancellablePointer &_cancel_ptr)
        :connection(_connection),
         balancer(_balancer),
         request(_request),
         body(request.pool, request.body) {
        _cancel_ptr = *this;
    }

    void Destroy() {
        DeleteFromPool(request.pool, this);
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        body.Clear();
        CancellablePointer c(std::move(cancel_ptr));
        Destroy();
        c.Cancel();
    }

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

static bool
send_fallback(HttpServerRequest *request,
              const LbFallbackConfig *fallback)
{
    if (!fallback->IsDefined())
        return false;

    http_server_simple_response(*request, fallback->status,
                                fallback->location.empty() ? nullptr : fallback->location.c_str(),
                                fallback->message.empty() ? nullptr : fallback->message.c_str());
    return true;
}

/**
 * Generate a cookie for sticky worker selection.  Return only worker
 * numbers that are not known to be failing.  Returns 0 on total
 * failure.
 */
static unsigned
generate_cookie(const AddressList *list)
{
    assert(list->GetSize() >= 2);

    const unsigned first = lb_cookie_generate(list->GetSize());

    unsigned i = first;
    do {
        assert(i >= 1 && i <= list->GetSize());
        const SocketAddress address = list->addresses[i % list->GetSize()];
        if (failure_get_status(address) == FAILURE_OK &&
            bulldog_check(address.GetAddress(), address.GetSize()) &&
            !bulldog_is_fading(address.GetAddress(), address.GetSize()))
            return i;

        i = lb_cookie_next(list->GetSize(), i);
    } while (i != first);

    /* all nodes have failed */
    return first;
}

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
LbRequest::OnHttpResponse(http_status_t status, StringMap &&_headers,
                          Istream *response_body)
{
    HttpHeaders headers(std::move(_headers));

    if (request.method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers.MoveToBuffer("content-length");

    if (new_cookie != 0) {
        char buffer[64];
        /* "Discard" must be last, to work around an Android bug*/
        snprintf(buffer, sizeof(buffer),
                 "beng_lb_node=0-%x; HttpOnly; Path=/; Version=1; Discard",
                 new_cookie);

        headers.Write("cookie2", "$Version=\"1\"");
        headers.Write("set-cookie", buffer);
    }

    http_server_response(&request, status, std::move(headers), response_body);
    Destroy();
}

void
LbRequest::OnHttpError(GError *error)
{
    if (is_server_failure(error))
        failure_add(tcp_stock_item_get_address(*stock_item));

    lb_connection_log_gerror(2, &connection, "Error", error);

    if (!send_fallback(&request, &cluster->fallback)) {
        const char *msg = connection.listener.verbose_response
            ? error->message
            : "Server failure";

        http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY, msg);
    }

    g_error_free(error);
    Destroy();
}

/*
 * stock callback
 *
 */

void
LbRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;

    const char *peer_subject = connection.ssl_filter != nullptr
        ? ssl_filter_get_peer_subject(connection.ssl_filter)
        : nullptr;
    const char *peer_issuer_subject = connection.ssl_filter != nullptr
        ? ssl_filter_get_peer_issuer_subject(connection.ssl_filter)
        : nullptr;

    auto &headers = request.headers;
    lb_forward_request_headers(request.pool, headers,
                               request.local_host_and_port,
                               request.remote_host,
                               peer_subject, peer_issuer_subject,
                               cluster->mangle_via);

    auto *lease = NewFromPool<StockItemLease>(request.pool, item);

    http_client_request(request.pool,
                        connection.instance.event_loop,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *lease,
                        item.GetStockName(),
                        NULL, NULL,
                        request.method, request.uri,
                        HttpHeaders(std::move(headers)),
                        body.Steal(), true,
                        *this, cancel_ptr);
}

void
LbRequest::OnStockItemError(GError *error)
{
    lb_connection_log_gerror(2, &connection, "Connect error", error);

    body.Clear();

    if (!send_fallback(&request, &cluster->fallback)) {
        const char *msg = connection.listener.verbose_response
            ? error->message
            : "Connection failure";

        http_server_send_message(&request, HTTP_STATUS_BAD_GATEWAY,
                                 msg);
    }

    g_error_free(error);
}

/*
 * http connection handler
 *
 */

void
LbConnection::HandleHttpRequest(HttpServerRequest &request,
                                CancellablePointer &cancel_ptr)
{
    ++instance.http_request_counter;

    request_start_time = std::chrono::steady_clock::now();

    const auto &goto_ = listener.destination.FindRequestLeaf(request);
    if (goto_.status != http_status_t(0)) {
        http_server_simple_response(request, goto_.status, nullptr, nullptr);
        return;
    }

    const auto request2 =
        NewFromPool<LbRequest>(request.pool,
                               *this, *instance.tcp_balancer,
                               request, cancel_ptr);
    const auto *cluster = request2->cluster = goto_.cluster;

    SocketAddress bind_address = SocketAddress::Null();
    const bool transparent_source = cluster->transparent_source;
    if (transparent_source) {
        bind_address = request.remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetFamily() == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(&request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        } else if (bind_address.GetFamily() == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(&request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin6_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        }
    }

    if (cluster->HasZeroConf()) {
        /* TODO: generalize the Zeroconf code, implement sticky */

        auto *cluster2 = instance.clusters.Find(cluster->name);
        if (cluster2 == nullptr) {
            http_server_send_message(&request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Zeroconf cluster not found");
            return;
        }

        const auto member = cluster2->Pick();
        if (member.first == nullptr) {
            http_server_send_message(&request,
                                     HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                     "Zeroconf cluster is empty");
            return;
        }

        assert(member.second.IsDefined());

        tcp_stock_get(instance.tcp_stock, &request.pool, member.first,
                      transparent_source, bind_address,
                      member.second,
                      20,
                      *request2, request2->cancel_ptr);

        return;
    }

    /* prepare for the balancer */

    unsigned session_sticky = 0;
    switch (cluster->address_list.sticky_mode) {
    case StickyMode::NONE:
    case StickyMode::FAILOVER:
        /* these modes require no preparation; they are handled
           completely by balancer_get() */
        break;

    case StickyMode::SOURCE_IP:
        /* calculate session_sticky from remote address */
        session_sticky = socket_address_sticky(request.remote_address);
        break;

    case StickyMode::SESSION_MODULO:
        /* calculate session_sticky from beng-proxy session id */
        session_sticky = lb_session_get(request.headers,
                                        cluster->session_cookie.c_str());
        break;

    case StickyMode::COOKIE:
        /* calculate session_sticky from beng-lb cookie */
        session_sticky = lb_cookie_get(request.headers);
        if (session_sticky == 0)
            request2->new_cookie = session_sticky =
                generate_cookie(&cluster->address_list);

        break;

    case StickyMode::JVM_ROUTE:
        /* calculate session_sticky from JSESSIONID cookie suffix */
        session_sticky = lb_jvm_route_get(request.headers, *cluster);
        break;
    }

    tcp_balancer_get(request2->balancer, request.pool,
                     transparent_source,
                     bind_address,
                     session_sticky,
                     cluster->address_list,
                     20,
                     *request2, request2->cancel_ptr);
}

void
LbConnection::LogHttpRequest(HttpServerRequest &request,
                             http_status_t status, int64_t length,
                             uint64_t bytes_received, uint64_t bytes_sent)
{
    access_log(&request, nullptr,
               request.headers.Get("referer"),
               request.headers.Get("user-agent"),
               status, length,
               bytes_received, bytes_sent,
               std::chrono::steady_clock::now() - request_start_time);
}

void
LbConnection::HttpConnectionError(GError *error)
{
    int level = 2;

    if (error->domain == errno_quark() && error->code == ECONNRESET)
        level = 4;

    lb_connection_log_gerror(level, this, "Error", error);
    g_error_free(error);

    assert(http != nullptr);
    http = nullptr;

    lb_connection_remove(this);
}

void
LbConnection::HttpConnectionClosed()
{
    assert(http != nullptr);
    http = nullptr;

    lb_connection_remove(this);
}
