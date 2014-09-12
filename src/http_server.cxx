/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_server_internal.hxx"
#include "istream-internal.h"
#include "strmap.h"
#include "address.h"
#include "gerrno.h"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>

const struct timeval http_server_idle_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

const struct timeval http_server_header_timeout = {
    .tv_sec = 20,
    .tv_usec = 0,
};

const struct timeval http_server_read_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

const struct timeval http_server_write_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

void
http_server_log(struct http_server_connection *connection)
{
    if (connection->handler == nullptr)
        /* this can happen when called via
           http_server_connection_close() (during daemon shutdown) */
        return;

    if (connection->handler->log == nullptr)
        return;

    connection->handler->log(connection->request.request,
                             connection->response.status,
                             connection->response.length,
                             connection->request.bytes_received,
                             connection->response.bytes_sent,
                             connection->handler_ctx);
}

struct http_server_request *
http_server_request_new(struct http_server_connection *connection)
{
    assert(connection != nullptr);

    connection->response.status = http_status_t(0);

    struct pool *pool = pool_new_linear(connection->pool,
                                        "http_server_request", 32768);
    pool_set_major(pool);

    auto request = NewFromPool<struct http_server_request>(pool);
    request->pool = pool;
    request->connection = connection;
    request->local_address = connection->local_address;
    request->local_address_length = connection->local_address_length;
    request->remote_address = connection->remote_address;
    request->remote_address_length = connection->remote_address_length;
    request->local_host_and_port = connection->local_host_and_port;
    request->remote_host_and_port = connection->remote_host_and_port;
    request->remote_host = connection->remote_host;
    request->headers = strmap_new(pool, 64);

    return request;
}

bool
http_server_try_write(struct http_server_connection *connection)
{
    assert(connection != nullptr);
    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state != http_server_connection::Request::START &&
           connection->request.read_state != http_server_connection::Request::HEADERS);
    assert(connection->request.request != nullptr);
    assert(connection->response.istream != nullptr);

    pool_ref(connection->pool);
    istream_read(connection->response.istream);

    bool valid = http_server_connection_valid(connection);
    pool_unref(connection->pool);

    return valid;
}

/*
 * buffered_socket handler
 *
 */

static BufferedResult
http_server_socket_data(const void *data, size_t length, void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    if (connection->response.pending_drained) {
        /* discard all incoming data while we're waiting for the
           (filtered) response to be drained */
        filtered_socket_consumed(&connection->socket, length);
        return BufferedResult::OK;
    }

    return http_server_feed(connection, data, length);
}

static DirectResult
http_server_socket_direct(int fd, enum istream_direct fd_type, void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(connection->request.read_state != http_server_connection::Request::END);
    assert(!connection->response.pending_drained);

    return http_server_try_request_direct(connection, fd, fd_type);
}

static bool
http_server_socket_write(void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(!connection->response.pending_drained);

    connection->response.want_write = false;

    if (!http_server_try_write(connection))
        return false;

    if (!connection->response.want_write)
        filtered_socket_unschedule_write(&connection->socket);

    return true;
}

static bool
http_server_socket_drained(void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    if (connection->response.pending_drained) {
        http_server_done(connection);
        return false;
    }

    return true;
}

static bool
http_server_socket_timeout(void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    daemon_log(4, "timeout on HTTP connection from '%s'\n",
               connection->remote_host_and_port);
    http_server_cancel(connection);
    return false;
}

static bool
http_server_socket_closed(void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    http_server_cancel(connection);
    return false;
}

static void
http_server_socket_error(GError *error, void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    http_server_error(connection, error);
}

static constexpr BufferedSocketHandler http_server_socket_handler = {
    .data = http_server_socket_data,
    .direct = http_server_socket_direct,
    .closed = http_server_socket_closed,
    .timeout = http_server_socket_timeout,
    .write = http_server_socket_write,
    .drained = http_server_socket_drained,
    .error = http_server_socket_error,
};

static void
http_server_timeout_callback(int fd gcc_unused, short event gcc_unused,
                             void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    daemon_log(4, "%s timeout on HTTP connection from '%s'\n",
               connection->request.read_state == http_server_connection::Request::START
               ? "idle"
               : (connection->request.read_state == http_server_connection::Request::HEADERS
                  ? "header" : "read"),
               connection->remote_host_and_port);
    http_server_cancel(connection);
    pool_commit();
}

void
http_server_connection_new(struct pool *pool, int fd, enum istream_direct fd_type,
                           const struct socket_filter *filter,
                           void *filter_ctx,
                           const struct sockaddr *local_address,
                           size_t local_address_length,
                           const struct sockaddr *remote_address,
                           size_t remote_address_length,
                           bool date_header,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           struct http_server_connection **connection_r)
{
    assert(fd >= 0);
    assert(handler != nullptr);
    assert(handler->request != nullptr);
    assert(handler->error != nullptr);
    assert(handler->free != nullptr);
    assert((local_address == nullptr) == (local_address_length == 0));

    auto connection = NewFromPool<struct http_server_connection>(pool);
    connection->pool = pool;

    filtered_socket_init(&connection->socket, pool, fd, fd_type,
                         nullptr, &http_server_write_timeout,
                         filter, filter_ctx,
                         &http_server_socket_handler, connection);

    connection->handler = handler;
    connection->handler_ctx = ctx;

    connection->local_address = local_address != nullptr
        ? (const struct sockaddr *)p_memdup(pool, local_address,
                                            local_address_length)
        : nullptr;
    connection->local_address_length = local_address_length;

    connection->remote_address = remote_address != nullptr
        ? (const struct sockaddr *)p_memdup(pool, remote_address,
                                            remote_address_length)
        : nullptr;
    connection->remote_address_length = remote_address_length;

    connection->local_host_and_port = local_address != nullptr
        ? address_to_string(pool, local_address, local_address_length)
        : nullptr;
    connection->remote_host_and_port = remote_address != nullptr
        ? address_to_string(pool, remote_address, remote_address_length)
        : nullptr;
    connection->remote_host = remote_address != nullptr
        ? address_to_host_string(pool, remote_address, remote_address_length)
        : nullptr;
    connection->date_header = date_header;
    connection->request.read_state = http_server_connection::Request::START;
    connection->request.request = nullptr;
    connection->request.bytes_received = 0;
    connection->response.istream = nullptr;
    connection->response.bytes_sent = 0;

    evtimer_set(&connection->idle_timeout,
                http_server_timeout_callback, connection);
    evtimer_add(&connection->idle_timeout, &http_server_idle_timeout);

    connection->score = HTTP_SERVER_NEW;

    *connection_r = connection;

    filtered_socket_read(&connection->socket, false);
}

static void
http_server_socket_close(struct http_server_connection *connection)
{
    assert(filtered_socket_connected(&connection->socket));

    filtered_socket_close(&connection->socket);

    evtimer_del(&connection->idle_timeout);
}

static void
http_server_socket_destroy(struct http_server_connection *connection)
{
    assert(filtered_socket_valid(&connection->socket));

    if (filtered_socket_connected(&connection->socket))
        http_server_socket_close(connection);

    filtered_socket_destroy(&connection->socket);
}

static void
http_server_request_close(struct http_server_connection *connection)
{
    assert(connection->request.read_state != http_server_connection::Request::START);
    assert(connection->request.request != nullptr);

    if (connection->response.status != http_status_t(0) &&
        connection->response.length >= 0)
        http_server_log(connection);

    struct pool *pool = connection->request.request->pool;
    pool_trash(pool);
    pool_unref(pool);
    connection->request.request = nullptr;

    if ((connection->request.read_state == http_server_connection::Request::BODY ||
         connection->request.read_state == http_server_connection::Request::END)) {
        if (connection->response.istream != nullptr)
            istream_free_handler(&connection->response.istream);
        else if (async_ref_defined(&connection->request.async_ref))
            /* don't call this if coming from
               _response_stream_abort() */
            async_abort(&connection->request.async_ref);
    }

    /* the handler must have closed the request body */
    assert(connection->request.read_state != http_server_connection::Request::BODY);
}

void
http_server_done(struct http_server_connection *connection)
{
    assert(connection != nullptr);
    assert(connection->handler != nullptr);
    assert(connection->handler->free != nullptr);
    assert(connection->request.read_state == http_server_connection::Request::START);

    /* shut down the socket gracefully to allow the TCP stack to
       transfer remaining response data */
    connection->socket.Shutdown();

    http_server_socket_destroy(connection);

    const struct http_server_connection_handler *handler = connection->handler;
    connection->handler = nullptr;

    handler->free(connection->handler_ctx);
}

void
http_server_cancel(struct http_server_connection *connection)
{
    assert(connection != nullptr);
    assert(connection->handler != nullptr);
    assert(connection->handler->free != nullptr);

    http_server_socket_destroy(connection);

    pool_ref(connection->pool);

    if (connection->request.read_state != http_server_connection::Request::START)
        http_server_request_close(connection);

    if (connection->handler != nullptr) {
        connection->handler->free(connection->handler_ctx);
        connection->handler = nullptr;
    }

    pool_unref(connection->pool);
}

void
http_server_error(struct http_server_connection *connection, GError *error)
{
    assert(connection != nullptr);
    assert(connection->handler != nullptr);
    assert(connection->handler->free != nullptr);

    http_server_socket_destroy(connection);

    pool_ref(connection->pool);

    if (connection->request.read_state != http_server_connection::Request::START)
        http_server_request_close(connection);

    if (connection->handler != nullptr) {
        g_prefix_error(&error, "error on HTTP connection from '%s': ",
                       connection->remote_host_and_port);

        const struct http_server_connection_handler *handler = connection->handler;
        void *handler_ctx = connection->handler_ctx;
        connection->handler = nullptr;
        connection->handler_ctx = nullptr;
        handler->error(error, handler_ctx);
    } else
        g_error_free(error);

    pool_unref(connection->pool);
}

void
http_server_error_message(struct http_server_connection *connection,
                          const char *msg)
{
    GError *error = g_error_new_literal(http_server_quark(), 0, msg);
    http_server_error(connection, error);
}

void
http_server_connection_close(struct http_server_connection *connection)
{
    assert(connection != nullptr);

    http_server_socket_destroy(connection);

    connection->handler = nullptr;

    if (connection->request.read_state != http_server_connection::Request::START)
        http_server_request_close(connection);
}

void
http_server_errno(struct http_server_connection *connection, const char *msg)
{
    if (errno == EPIPE || errno == ECONNRESET) {
        /* don't report this common problem */
        http_server_cancel(connection);
        return;
    }

    GError *error = new_error_errno_msg(msg);
    http_server_error(connection, error);
}

void
http_server_connection_graceful(struct http_server_connection *connection)
{
    assert(connection != nullptr);

    if (connection->request.read_state == http_server_connection::Request::START)
        /* there is no request currently; close the connection
           immediately */
        http_server_done(connection);
    else
        /* a request is currently being handled; disable keep_alive so
           the connection will be closed after this last request */
        connection->keep_alive = false;
}

enum http_server_score
http_server_connection_score(const struct http_server_connection *connection)
{
    return connection->score;
}
