/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-request.h"
#include "http-response.h"
#include "header-writer.h"
#include "tcp-stock.h"
#include "tcp-balancer.h"
#include "stock.h"
#include "async.h"
#include "http-client.h"
#include "uri-address.h"
#include "growing-buffer.h"
#include "lease.h"
#include "abort-close.h"
#include "failure.h"
#include "address_envelope.h"
#include "istream.h"

#include <inline/compiler.h>

#include <string.h>

struct http_request {
    struct pool *pool;

    struct tcp_balancer *tcp_balancer;

    unsigned session_sticky;

    struct stock_item *stock_item;
    const struct address_envelope *current_address;

    http_method_t method;
    const struct uri_with_address *uwa;
    struct growing_buffer *headers;
    struct istream *body;

    unsigned retries;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
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

static const struct stock_get_handler http_request_stock_handler;

/*
 * HTTP response handler
 *
 */

static void
http_request_response_response(http_status_t status, struct strmap *headers,
                               struct istream *body, void *ctx)
{
    struct http_request *hr = ctx;

    failure_unset(&hr->current_address->address,
                  hr->current_address->length,
                  FAILURE_RESPONSE);

    http_response_handler_invoke_response(&hr->handler,
                                          status, headers, body);
}

static void
http_request_response_abort(GError *error, void *ctx)
{
    struct http_request *hr = ctx;

    if (hr->retries > 0 && hr->body == NULL &&
        error->domain == http_client_quark() &&
        error->code == HTTP_CLIENT_REFUSED) {
        /* the server has closed the connection prematurely, maybe
           because it didn't want to get any further requests on that
           TCP connection.  Let's try again. */

        g_error_free(error);

        --hr->retries;
        tcp_balancer_get(hr->tcp_balancer, hr->pool,
                         hr->session_sticky,
                         &hr->uwa->addresses,
                         30,
                         &http_request_stock_handler, hr,
                         hr->async_ref);
    } else {
        if (is_server_failure(error))
            failure_set(&hr->current_address->address,
                        hr->current_address->length,
                        FAILURE_RESPONSE, 20);

        http_response_handler_invoke_abort(&hr->handler, error);
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
    struct http_request *hr = ctx;

    tcp_balancer_put(hr->tcp_balancer, hr->stock_item, !reuse);
}

static const struct lease http_socket_lease = {
    .release = http_socket_release,
};


/*
 * stock callback
 *
 */

static void
http_request_stock_ready(struct stock_item *item, void *ctx)
{
    struct http_request *hr = ctx;

    hr->stock_item = item;
    hr->current_address = tcp_balancer_get_last();

    http_client_request(hr->pool,
                        tcp_stock_item_get(item),
                        tcp_stock_item_get_domain(item) == AF_LOCAL
                        ? ISTREAM_SOCKET : ISTREAM_TCP,
                        &http_socket_lease, hr,
                        hr->method, hr->uwa->path, hr->headers,
                        hr->body, true,
                        &http_request_response_handler, hr,
                        hr->async_ref);
}

static void
http_request_stock_error(GError *error, void *ctx)
{
    struct http_request *hr = ctx;

    if (hr->body != NULL)
        istream_close_unused(hr->body);

    http_response_handler_invoke_abort(&hr->handler, error);
}

static const struct stock_get_handler http_request_stock_handler = {
    .ready = http_request_stock_ready,
    .error = http_request_stock_error,
};


/*
 * constructor
 *
 */

void
http_request(struct pool *pool,
             struct tcp_balancer *tcp_balancer,
             unsigned session_sticky,
             http_method_t method,
             const struct uri_with_address *uwa,
             struct growing_buffer *headers,
             struct istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    struct http_request *hr;

    assert(uwa != NULL);
    assert(uwa->host_and_port != NULL);
    assert(uwa->path != NULL);
    assert(handler != NULL);
    assert(handler->response != NULL);
    assert(body == NULL || !istream_has_handler(body));

    hr = p_malloc(pool, sizeof(*hr));
    hr->pool = pool;
    hr->tcp_balancer = tcp_balancer;
    hr->session_sticky = session_sticky;
    hr->method = method;
    hr->uwa = uwa;

    hr->headers = headers;
    if (hr->headers == NULL)
        hr->headers = growing_buffer_new(pool, 512);

    http_response_handler_set(&hr->handler, handler, handler_ctx);
    hr->async_ref = async_ref;

    if (body != NULL) {
        hr->body = istream_hold_new(pool, body);
        async_ref = async_close_on_abort(pool, hr->body, async_ref);
    } else
        hr->body = NULL;

    if (uwa->host_and_port != NULL)
        header_write(hr->headers, "host", uwa->host_and_port);

    header_write(hr->headers, "connection", "keep-alive");

    hr->retries = 2;
    tcp_balancer_get(tcp_balancer, pool, session_sticky,
                     &uwa->addresses,
                     30,
                     &http_request_stock_handler, hr,
                     async_ref);
}
