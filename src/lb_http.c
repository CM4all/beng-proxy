/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_http.h"
#include "lb_instance.h"
#include "lb_connection.h"
#include "lb_config.h"
#include "lb_session.h"
#include "address-envelope.h"
#include "http-server.h"
#include "http-client.h"
#include "tcp-stock.h"
#include "tcp-balancer.h"
#include "lease.h"
#include "header-writer.h"
#include "http-response.h"
#include "stock.h"
#include "clock.h"
#include "access-log.h"

#include <http/status.h>
#include <daemon/log.h>

struct lb_request {
    struct lb_connection *connection;

    struct http_server_request *request;

    struct async_operation_ref *async_ref;

    struct stock_item *stock_item;
};

/*
 * socket lease
 *
 */

static void
my_socket_release(bool reuse, void *ctx)
{
    struct lb_request *request2 = ctx;

    tcp_balancer_put(request2->connection->instance->tcp_balancer,
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
                     istream_t body, void *ctx)
{
    struct lb_request *request2 = ctx;
    struct http_server_request *request = request2->request;

    struct growing_buffer *headers2 = headers_dup(request->pool, headers);

    http_server_response(request2->request, status, headers2, body);
}

static void
my_response_abort(GError *error, void *ctx)
{
    struct lb_request *request2 = ctx;

    daemon_log(2, "error on %s: %s\n", request2->request->uri, error->message);
    g_error_free(error);

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
    struct lb_request *request2 = ctx;
    struct http_server_request *request = request2->request;

    request2->stock_item = item;

    struct growing_buffer *headers2 = headers_dup(request->pool,
                                                  request->headers);

    http_client_request(request->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &my_socket_lease, request2,
                        request->method, request->uri,
                        headers2, request->body,
                        &my_response_handler, request2,
                        request2->async_ref);
}

static void
my_stock_error(GError *error, void *ctx)
{
    struct lb_request *request2 = ctx;

    daemon_log(2, "Connection failure: %s\n", error->message);
    g_error_free(error);

    if (request2->request->body != NULL)
        istream_close_unused(request2->request->body);

    http_server_send_message(request2->request, HTTP_STATUS_BAD_GATEWAY,
                             "Connection failure");
}

static const struct stock_handler my_stock_handler = {
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
    struct lb_connection *connection = ctx;

    connection->request_start_time = now_us();

    const struct lb_cluster_config *cluster = connection->listener->cluster;
    assert(cluster != NULL);
    assert(cluster->num_members > 0);

    struct lb_request *request2 = p_malloc(request->pool, sizeof(*request2));
    request2->connection = connection;
    request2->request = request;
    request2->async_ref = async_ref;

    unsigned session_sticky =
        cluster->address_list.sticky_mode == STICKY_SESSION_MODULO
        ? lb_session_get(request->headers)
        : 0;

    tcp_balancer_get(connection->instance->tcp_balancer, request->pool,
                     session_sticky,
                     &cluster->address_list,
                     &my_stock_handler, request2,
                     async_ref);
}

static void
lb_http_connection_log(struct http_server_request *request,
                       http_status_t status, off_t length,
                       uint64_t bytes_received, uint64_t bytes_sent,
                       void *ctx)
{
    struct lb_connection *connection = ctx;

    access_log(request, NULL,
               strmap_get_checked(request->headers, "referer"),
               strmap_get_checked(request->headers, "user-agent"),
               status, length,
               bytes_received, bytes_sent,
               now_us() - connection->request_start_time);
}

static void
lb_http_connection_free(void *ctx)
{
    struct lb_connection *connection = ctx;
    pool_t pool;

    assert(connection->http != NULL);
    assert(connection->instance != NULL);
    assert(connection->instance->num_connections > 0);

    connection->http = NULL;

    list_remove(&connection->siblings);
    --connection->instance->num_connections;

    pool = connection->pool;
    pool_trash(pool);
    pool_unref(pool);
}

const struct http_server_connection_handler lb_http_connection_handler = {
    .request = lb_http_connection_request,
    .log = lb_http_connection_log,
    .free = lb_http_connection_free,
};
