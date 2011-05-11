/*
 * Handler for HTTP requests.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_handler.h"
#include "lb_instance.h"
#include "lb_connection.h"
#include "lb_config.h"
#include "address-envelope.h"
#include "http-server.h"
#include "http-client.h"
#include "tcp-stock.h"
#include "lease.h"
#include "header-writer.h"
#include "http-response.h"
#include "stock.h"

#include <http/status.h>
#include <daemon/log.h>

#include <netinet/in.h>

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

    tcp_stock_put(request2->connection->instance->tcp_stock,
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

    http_server_send_message(request2->request, HTTP_STATUS_BAD_GATEWAY,
                             "Connection failure");
}

static const struct stock_handler my_stock_handler = {
    .ready = my_stock_ready,
    .error = my_stock_error,
};

/*
 * public
 *
 */

static const struct sockaddr *
set_port(struct pool *pool, const struct sockaddr *address,
         size_t address_length, unsigned port)
{
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;

    switch (address->sa_family) {
    case AF_INET:
        sa4 = p_memdup(pool, address, address_length);
        sa4->sin_port = htons(port);
        return (const struct sockaddr *)sa4;

    case AF_INET6:
        sa6 = p_memdup(pool, address, address_length);
        sa6->sin6_port = htons(port);
        return (const struct sockaddr *)sa6;
    }

    return address;
}

void
handle_http_request(struct lb_connection *connection,
                    struct http_server_request *request,
                    struct async_operation_ref *async_ref)
{
    const struct lb_cluster_config *cluster = connection->listener->cluster;
    assert(cluster != NULL);
    assert(cluster->num_members > 0);

    const struct lb_member_config *member = &cluster->members[0];
    const struct address_envelope *envelope = member->node->envelope;

    const struct sockaddr *address = set_port(request->pool,
                                              &envelope->address,
                                              envelope->length,
                                              member->port);

    struct lb_request *request2 = p_malloc(request->pool, sizeof(*request2));
    request2->connection = connection;
    request2->request = request;
    request2->async_ref = async_ref;

    tcp_stock_get(connection->instance->tcp_stock, request->pool, NULL,
                  address, envelope->length,
                  &my_stock_handler, request2,
                  async_ref);
}
