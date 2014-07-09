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
#include "address_envelope.hxx"
#include "address_sticky.h"
#include "http_server.hxx"
#include "http_client.hxx"
#include "tcp_stock.hxx"
#include "tcp_balancer.hxx"
#include "lease.h"
#include "header_writer.hxx"
#include "http_response.hxx"
#include "stock.hxx"
#include "clock.h"
#include "access-log.h"
#include "strmap.hxx"
#include "failure.hxx"
#include "bulldog.h"
#include "abort-close.h"

#include <http/status.h>
#include <daemon/log.h>

struct lb_request {
    struct lb_connection *connection;
    const lb_cluster_config *cluster;

    struct tcp_balancer *balancer;

    struct http_server_request *request;

    /**
     * The request body (wrapperd with istream_hold).
     */
    struct istream *body;

    struct async_operation_ref *async_ref;

    struct stock_item *stock_item;
    const struct address_envelope *current_address;

    unsigned new_cookie;
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
        return strmap_get(request.headers, reference.name.c_str());
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
generate_cookie(const struct address_list *list)
{
    assert(list->GetSize() >= 2);

    const unsigned first = lb_cookie_generate(list->GetSize());

    unsigned i = first;
    do {
        assert(i >= 1 && i <= list->GetSize());
        const struct address_envelope *envelope =
            list->addresses[i % list->GetSize()];
        if (!failure_check(&envelope->address, envelope->length) &&
            bulldog_check(&envelope->address, envelope->length) &&
            !bulldog_is_fading(&envelope->address, envelope->length))
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
 * socket lease
 *
 */

static void
my_socket_release(bool reuse, void *ctx)
{
    struct lb_request *request2 = (struct lb_request *)ctx;

    tcp_balancer_put(request2->balancer,
                     request2->stock_item, !reuse);
}

static const struct lease my_socket_lease = {
    .release = my_socket_release,
};

/*
 * HTTP response handler
 *
 */

static void
my_response_response(http_status_t status, struct strmap *headers,
                     struct istream *body, void *ctx)
{
    struct lb_request *request2 = (struct lb_request *)ctx;
    struct http_server_request *request = request2->request;

    struct growing_buffer *headers2 = headers_dup(request->pool, headers);
    if (request2->request->method == HTTP_METHOD_HEAD)
        /* pass Content-Length, even though there is no response body
           (RFC 2616 14.13) */
        headers_copy_one(headers, headers2, "content-length");

    if (request2->new_cookie != 0) {
        char buffer[64];
        /* "Discard" must be last, to work around an Android bug*/
        snprintf(buffer, sizeof(buffer),
                 "beng_lb_node=0-%x; HttpOnly; Path=/; Version=1; Discard",
                 request2->new_cookie);

        header_write(headers2, "cookie2", "$Version=\"1\"");
        header_write(headers2, "set-cookie", buffer);
    }

    http_server_response(request2->request, status, headers2, body);
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct lb_request *request2 = (struct lb_request *)ctx;
    const struct lb_connection *connection = request2->connection;

    if (is_server_failure(error))
        failure_add(&request2->current_address->address,
                    request2->current_address->length);


    lb_connection_log_gerror(2, connection, "Error", error);
    g_error_free(error);

    if (!send_fallback(request2->request,
                       &request2->cluster->fallback))
        http_server_send_message(request2->request, HTTP_STATUS_BAD_GATEWAY,
                                 "Server failure");
}

static const struct http_response_handler my_response_handler = {
    .response = my_response_response,
    .abort = my_response_abort,
};

/*
 * stock callback
 *
 */

static void
my_stock_ready(struct stock_item *item, void *ctx)
{
    struct lb_request *request2 = (struct lb_request *)ctx;
    struct http_server_request *request = request2->request;

    request2->stock_item = item;
    request2->current_address = tcp_balancer_get_last();

    const char *peer_subject = request2->connection->ssl_filter != nullptr
        ? ssl_filter_get_peer_subject(request2->connection->ssl_filter)
        : nullptr;
    const char *peer_issuer_subject = request2->connection->ssl_filter != nullptr
        ? ssl_filter_get_peer_issuer_subject(request2->connection->ssl_filter)
        : nullptr;

    struct strmap *headers =
        lb_forward_request_headers(request->pool, request->headers,
                                   request->local_host_and_port,
                                   request->remote_host,
                                   peer_subject, peer_issuer_subject,
                                   request2->cluster->mangle_via);

    struct growing_buffer *headers2 = headers_dup(request->pool, headers);

    http_client_request(request->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &my_socket_lease, request2,
                        NULL, NULL,
                        request->method, request->uri,
                        headers2, request2->body, true,
                        &my_response_handler, request2,
                        request2->async_ref);
}

static void
my_stock_error(GError *error, void *ctx)
{
    struct lb_request *request2 = (struct lb_request *)ctx;
    const struct lb_connection *connection = request2->connection;

    lb_connection_log_gerror(2, connection, "Connect error", error);
    g_error_free(error);

    if (request2->body != nullptr)
        istream_close_unused(request2->body);

    if (!send_fallback(request2->request, &request2->cluster->fallback))
        http_server_send_message(request2->request, HTTP_STATUS_BAD_GATEWAY,
                                 "Connection failure");
}

static const struct stock_get_handler my_stock_handler = {
    .ready = my_stock_ready,
    .error = my_stock_error,
};

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

    lb_request *request2 = NewFromPool<lb_request>(request->pool);
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

    const struct sockaddr *bind_address = nullptr;
    size_t bind_address_size = 0;
    const bool transparent_source = cluster->transparent_source;
    if (transparent_source) {
        bind_address = request->remote_address;
        bind_address_size = request->remote_address_length;

        /* reset the port to 0 to allow the kernel to choose one */
        if (bind_address->sa_family == AF_INET) {
            struct sockaddr_in *s_in = (struct sockaddr_in *)
                p_memdup(request->pool, bind_address, bind_address_size);
            s_in->sin_port = 0;
            bind_address = (const struct sockaddr *)s_in;
        } else if (bind_address->sa_family == AF_INET6) {
            struct sockaddr_in6 *s_in = (struct sockaddr_in6 *)
                p_memdup(request->pool, bind_address, bind_address_size);
            s_in->sin6_port = 0;
            bind_address = (const struct sockaddr *)s_in;
        }
    }

    unsigned session_sticky = 0;
    switch (cluster->address_list.sticky_mode) {
    case STICKY_NONE:
    case STICKY_FAILOVER:
        break;

    case STICKY_SOURCE_IP:
        session_sticky = socket_address_sticky(request->remote_address);
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

    tcp_balancer_get(request2->balancer, request->pool,
                     transparent_source,
                     bind_address, bind_address_size,
                     session_sticky,
                     &cluster->address_list,
                     20,
                     &my_stock_handler, request2,
                     async_optional_close_on_abort(request->pool,
                                                   request2->body,
                                                   async_ref));
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
