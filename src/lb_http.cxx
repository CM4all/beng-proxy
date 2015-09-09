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
#include "ssl_filter.hxx"
#include "address_sticky.h"
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
#include "stock.hxx"
#include "clock.h"
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
    struct lb_connection *connection;
    const lb_cluster_config *cluster;

    TcpBalancer *balancer;

    struct http_server_request *request;

    /**
     * The request body (wrapperd with istream_hold).
     */
    struct istream *body;

    struct async_operation_ref *async_ref;

    StockItem *stock_item;
    SocketAddress current_address;

    unsigned new_cookie;

    /* virtual methods from class StockGetHandler */
    void OnStockItemReady(StockItem &item) override;
    void OnStockItemError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool reuse) override {
        tcp_balancer_put(*balancer, *stock_item, !reuse);
    }
};

gcc_pure
static const char *
lb_http_get_attribute(const http_server_request &request,
                      const lb_attribute_reference &reference)
{
    switch (reference.type) {
    case lb_attribute_reference::Type::METHOD:
        return http_method_to_string(request.method);

    case lb_attribute_reference::Type::URI:
        return request.uri;

    case lb_attribute_reference::Type::HEADER:
        return request.headers->Get(reference.name.c_str());
    }

    assert(false);
    gcc_unreachable();
}

gcc_pure
static bool
lb_http_check_condition(const lb_condition_config &condition,
                        const http_server_request &request)
{
    const char *value = lb_http_get_attribute(request,
                                              condition.attribute_reference);
    if (value == nullptr)
        value = "";

    return condition.Match(value);
}

gcc_pure
static const lb_cluster_config *
lb_http_select_cluster(const lb_goto &destination,
                       const http_server_request &request);

gcc_pure
static const lb_cluster_config *
lb_http_select_cluster(const lb_branch_config &branch,
                       const http_server_request &request)
{
    for (const auto &i : branch.conditions)
        if (lb_http_check_condition(i.condition, request))
            return lb_http_select_cluster(i.destination, request);

    return lb_http_select_cluster(branch.fallback, request);
}

gcc_pure
static const lb_cluster_config *
lb_http_select_cluster(const lb_goto &destination,
                       const http_server_request &request)
{
    if (gcc_likely(destination.cluster != nullptr))
        return destination.cluster;

    assert(destination.branch != nullptr);
    return lb_http_select_cluster(*destination.branch, request);
}

static bool
send_fallback(struct http_server_request *request,
              const struct lb_fallback_config *fallback)
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
                     struct istream *body, void *ctx)
{
    LbRequest *request2 = (LbRequest *)ctx;
    struct http_server_request *request = request2->request;
    struct pool &pool = *request->pool;

    HttpHeaders headers(_headers);

    if (request2->request->method == HTTP_METHOD_HEAD)
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

    http_server_response(request2->request, status, std::move(headers), body);
}

static void
my_response_abort(GError *error, void *ctx)
{
    LbRequest *request2 = (LbRequest *)ctx;
    const struct lb_connection *connection = request2->connection;

    if (is_server_failure(error))
        failure_add(request2->current_address);

    lb_connection_log_gerror(2, connection, "Error", error);

    if (!send_fallback(request2->request,
                       &request2->cluster->fallback)) {
        const char *msg = connection->listener->verbose_response
            ? error->message
            : "Server failure";

        http_server_send_message(request2->request, HTTP_STATUS_BAD_GATEWAY,
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

    HttpHeaders headers =
        lb_forward_request_headers(request->pool, request->headers,
                                   request->local_host_and_port,
                                   request->remote_host,
                                   peer_subject, peer_issuer_subject,
                                   cluster->mangle_via);

    http_client_request(*request->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? FdType::FD_SOCKET : FdType::FD_TCP,
                        *this,
                        tcp_stock_item_get_name(item),
                        NULL, NULL,
                        request->method, request->uri,
                        std::move(headers), body, true,
                        my_response_handler, this,
                        *async_ref);
}

void
LbRequest::OnStockItemError(GError *error)
{
    lb_connection_log_gerror(2, connection, "Connect error", error);

    if (body != nullptr)
        istream_close_unused(body);

    if (!send_fallback(request, &cluster->fallback)) {
        const char *msg = connection->listener->verbose_response
            ? error->message
            : "Connection failure";

        http_server_send_message(request, HTTP_STATUS_BAD_GATEWAY,
                                 msg);
    }

    g_error_free(error);
}

/*
 * http connection handler
 *
 */

static void
lb_http_connection_request(struct http_server_request *request,
                           void *ctx,
                           struct async_operation_ref *async_ref)
{
    struct lb_connection *connection = (struct lb_connection *)ctx;

    ++connection->instance->http_request_counter;

    connection->request_start_time = now_us();

    const auto request2 = NewFromPool<LbRequest>(*request->pool);
    request2->connection = connection;
    const lb_cluster_config *cluster = request2->cluster =
        lb_http_select_cluster(connection->listener->destination, *request);
    request2->balancer = connection->instance->tcp_balancer;
    request2->request = request;
    request2->body = request->body != nullptr
        ? istream_hold_new(request->pool, request->body)
        : nullptr;
    request2->async_ref = async_ref;
    request2->new_cookie = 0;

    SocketAddress bind_address = SocketAddress::Null();
    const bool transparent_source = cluster->transparent_source;
    if (transparent_source) {
        bind_address = request->remote_address;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address.GetFamily() == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(request->pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        } else if (bind_address.GetFamily() == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(request->pool, bind_address.GetAddress(),
                         bind_address.GetSize());
            s_in->sin6_port = 0;
            bind_address = SocketAddress((const struct sockaddr *)s_in,
                                         bind_address.GetSize());
        }
    }

    unsigned session_sticky = 0;
    switch (cluster->address_list.sticky_mode) {
    case STICKY_NONE:
    case STICKY_FAILOVER:
        break;

    case STICKY_SOURCE_IP:
        session_sticky = socket_address_sticky(request->remote_address.GetAddress());
        break;

    case STICKY_SESSION_MODULO:
        session_sticky = lb_session_get(request->headers,
                                        cluster->session_cookie.c_str());
        break;

    case STICKY_COOKIE:
        session_sticky = lb_cookie_get(request->headers);
        if (session_sticky == 0)
            request2->new_cookie = session_sticky =
                generate_cookie(&cluster->address_list);

        break;

    case STICKY_JVM_ROUTE:
        session_sticky = lb_jvm_route_get(request->headers, cluster);
        break;
    }

    tcp_balancer_get(*request2->balancer, *request->pool,
                     transparent_source,
                     bind_address,
                     session_sticky,
                     cluster->address_list,
                     20,
                     *request2,
                     async_optional_close_on_abort(*request->pool,
                                                   request2->body,
                                                   *async_ref));
}

static void
lb_http_connection_log(struct http_server_request *request,
                       http_status_t status, off_t length,
                       uint64_t bytes_received, uint64_t bytes_sent,
                       void *ctx)
{
    struct lb_connection *connection = (struct lb_connection *)ctx;

    access_log(request, nullptr,
               strmap_get_checked(request->headers, "referer"),
               strmap_get_checked(request->headers, "user-agent"),
               status, length,
               bytes_received, bytes_sent,
               now_us() - connection->request_start_time);
}

static void
lb_http_connection_error(GError *error, void *ctx)
{
    struct lb_connection *connection = (struct lb_connection *)ctx;

    int level = 2;

    if (error->domain == errno_quark() && error->code == ECONNRESET)
        level = 4;

    lb_connection_log_gerror(2, connection, "Error", error);
    g_error_free(error);

    assert(connection->http != nullptr);
    connection->http = nullptr;

    lb_connection_remove(connection);
}

static void
lb_http_connection_free(void *ctx)
{
    struct lb_connection *connection = (struct lb_connection *)ctx;

    assert(connection->http != nullptr);

    connection->http = nullptr;

    lb_connection_remove(connection);
}

const struct http_server_connection_handler lb_http_connection_handler = {
    lb_http_connection_request,
    lb_http_connection_log,
    lb_http_connection_error,
    lb_http_connection_free,
};
