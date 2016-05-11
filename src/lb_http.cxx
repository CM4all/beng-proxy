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
#include "lease.hxx"
#include "header_writer.hxx"
#include "http_response.hxx"
#include "http_headers.hxx"
#include "stock/GetHandler.hxx"
#include "stock/Item.hxx"
#include "system/clock.h"
#include "access_log.hxx"
#include "strmap.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "abort_close.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "istream/istream.hxx"
#include "istream/istream_hold.hxx"
#include "gerrno.h"

#include <http/status.h>
#include <daemon/log.h>

struct LbRequest final : public StockGetHandler, Lease {
    LbConnection *connection;
    const LbClusterConfig *cluster;

    TcpBalancer *balancer;

    struct http_server_request &request;

    /**
     * The request body (wrapped with istream_hold).
     */
    Istream *body;

    struct async_operation_ref *async_ref;

    StockItem *stock_item;
    SocketAddress current_address;

    unsigned new_cookie = 0;

    LbRequest(LbConnection &_connection, TcpBalancer &_balancer,
              struct http_server_request &_request,
              struct async_operation_ref &_async_ref)
        :connection(&_connection),
         balancer(&_balancer),
         request(_request),
         body(request.body != nullptr
              ? istream_hold_new(*request.pool, *request.body)
              : nullptr),
         async_ref(&_async_ref) {}

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        stock_item->Put(!reuse);
    }
};

gcc_pure
static const char *
lb_http_get_attribute(const http_server_request &request,
                      const LbAttributeReference &reference)
{
    switch (reference.type) {
    case LbAttributeReference::Type::METHOD:
        return http_method_to_string(request.method);

    case LbAttributeReference::Type::URI:
        return request.uri;

    case LbAttributeReference::Type::HEADER:
        return request.headers->Get(reference.name.c_str());
    }

    assert(false);
    gcc_unreachable();
}

gcc_pure
static bool
lb_http_check_condition(const LbConditionConfig &condition,
                        const http_server_request &request)
{
    const char *value = lb_http_get_attribute(request,
                                              condition.attribute_reference);
    if (value == nullptr)
        value = "";

    return condition.Match(value);
}

gcc_pure
static const LbClusterConfig *
lb_http_select_cluster(const LbGoto &destination,
                       const http_server_request &request);

gcc_pure
static const LbClusterConfig *
lb_http_select_cluster(const LbBranchConfig &branch,
                       const http_server_request &request)
{
    for (const auto &i : branch.conditions)
        if (lb_http_check_condition(i.condition, request))
            return lb_http_select_cluster(i.destination, request);

    return lb_http_select_cluster(branch.fallback, request);
}

gcc_pure
static const LbClusterConfig *
lb_http_select_cluster(const LbGoto &destination,
                       const http_server_request &request)
{
    if (gcc_likely(destination.cluster != nullptr))
        return destination.cluster;

    assert(destination.branch != nullptr);
    return lb_http_select_cluster(*destination.branch, request);
}

static bool
send_fallback(struct http_server_request *request,
              const LbFallbackConfig *fallback)
{
    if (!fallback->location.empty()) {
        http_server_send_redirect(request, HTTP_STATUS_FOUND,
                                  fallback->location.c_str(), "Found");
        return true;
    } else if (!fallback->message.empty()) {
        /* custom status + error message */
        assert(http_status_is_valid(fallback->status));

        http_server_send_message(request, fallback->status,
                                 fallback->message.c_str());
        return true;
    } else
        return false;
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

static void
my_response_response(http_status_t status, struct strmap *_headers,
                     Istream *body, void *ctx)
{
    LbRequest *request2 = (LbRequest *)ctx;
    struct http_server_request *request = &request2->request;
    struct pool &pool = *request->pool;

    HttpHeaders headers(_headers);

    if (request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers.MoveToBuffer(pool, "content-length");

    if (request2->new_cookie != 0) {
        char buffer[64];
        /* "Discard" must be last, to work around an Android bug*/
        snprintf(buffer, sizeof(buffer),
                 "beng_lb_node=0-%x; HttpOnly; Path=/; Version=1; Discard",
                 request2->new_cookie);

        headers.Write(pool, "cookie2", "$Version=\"1\"");
        headers.Write(pool, "set-cookie", buffer);
    }

    http_server_response(request, status, std::move(headers), body);
}

static void
my_response_abort(GError *error, void *ctx)
{
    LbRequest *request2 = (LbRequest *)ctx;
    const LbConnection *connection = request2->connection;

    if (is_server_failure(error))
        failure_add(request2->current_address);

    lb_connection_log_gerror(2, connection, "Error", error);

    if (!send_fallback(&request2->request,
                       &request2->cluster->fallback)) {
        const char *msg = connection->listener.verbose_response
            ? error->message
            : "Server failure";

        http_server_send_message(&request2->request, HTTP_STATUS_BAD_GATEWAY,
                                 msg);
    }

    g_error_free(error);
}

static const struct http_response_handler my_response_handler = {
    .response = my_response_response,
    .abort = my_response_abort,
};

/*
 * stock callback
 *
 */

void
LbRequest::OnStockItemReady(StockItem &item)
{
    stock_item = &item;
    current_address = tcp_balancer_get_last();

    const char *peer_subject = connection->ssl_filter != nullptr
        ? ssl_filter_get_peer_subject(connection->ssl_filter)
        : nullptr;
    const char *peer_issuer_subject = connection->ssl_filter != nullptr
        ? ssl_filter_get_peer_issuer_subject(connection->ssl_filter)
        : nullptr;

    auto &headers = *request.headers;
    lb_forward_request_headers(*request.pool, headers,
                               request.local_host_and_port,
                               request.remote_host,
                               peer_subject, peer_issuer_subject,
                               cluster->mangle_via);

    http_client_request(*request.pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *this,
                        tcp_stock_item_get_name(item),
                        NULL, NULL,
                        request.method, request.uri,
                        HttpHeaders(headers), body, true,
                        my_response_handler, this,
                        *async_ref);
}

void
LbRequest::OnStockItemError(GError *error)
{
    lb_connection_log_gerror(2, connection, "Connect error", error);

    if (body != nullptr)
        body->CloseUnused();

    if (!send_fallback(&request, &cluster->fallback)) {
        const char *msg = connection->listener.verbose_response
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
LbConnection::HandleHttpRequest(struct http_server_request &request,
                                struct async_operation_ref &async_ref)
{
    ++instance.http_request_counter;

    request_start_time = now_us();

    const auto request2 =
        NewFromPool<LbRequest>(*request.pool,
                               *this, *instance.tcp_balancer,
                               request, async_ref);
    const auto *cluster = request2->cluster =
        lb_http_select_cluster(listener.destination, request);

    SocketAddress bind_address = SocketAddress::Null();
    const bool transparent_source = cluster->transparent_source;
    if (transparent_source) {
        bind_address = request.remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetFamily() == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        } else if (bind_address.GetFamily() == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(request.pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin6_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        }
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
        session_sticky = lb_jvm_route_get(request.headers, cluster);
        break;
    }

    tcp_balancer_get(*request2->balancer, *request.pool,
                     transparent_source,
                     bind_address,
                     session_sticky,
                     cluster->address_list,
                     20,
                     *request2,
                     async_optional_close_on_abort(*request.pool,
                                                   request2->body,
                                                   async_ref));
}

void
LbConnection::LogHttpRequest(struct http_server_request &request,
                             http_status_t status, off_t length,
                             uint64_t bytes_received, uint64_t bytes_sent)
{
    access_log(&request, nullptr,
               strmap_get_checked(request.headers, "referer"),
               strmap_get_checked(request.headers, "user-agent"),
               status, length,
               bytes_received, bytes_sent,
               now_us() - request_start_time);
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
