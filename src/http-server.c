/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "strmap.h"
#include "address.h"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

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

struct http_server_request *
http_server_request_new(struct http_server_connection *connection)
{
    struct pool *pool;
    struct http_server_request *request;

    assert(connection != NULL);

    pool = pool_new_linear(connection->pool, "http_server_request", 32768);
    pool_set_major(pool);
    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->connection = connection;
    request->local_address = connection->local_address;
    request->local_address_length = connection->local_address_length;
    request->local_host = connection->local_host;
    request->remote_address = connection->remote_address;
    request->remote_host = connection->remote_host;
    request->headers = strmap_new(pool, 64);

    return request;
}

bool
http_server_try_write(struct http_server_connection *connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.istream != NULL);

    pool_ref(connection->pool);
    istream_read(connection->response.istream);

    bool valid = http_server_connection_valid(connection);
    pool_unref(connection->pool);

    return valid;
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_write_event_callback2(struct http_server_connection *connection,
                                  short event)
{
    if (event & EV_TIMEOUT) {
        daemon_log(4, "write timeout on HTTP connection from %s\n",
                   connection->remote_host);
        http_server_cancel(connection);
        return false;
    }

    connection->response.want_write = false;

    if (!http_server_try_write(connection))
        return false;

    if (!connection->response.want_write)
        event_del(&connection->response.event);

    return true;
}

static void
http_server_write_event_callback(int fd gcc_unused, short event, void *ctx)
{
    struct http_server_connection *connection = ctx;

    http_server_write_event_callback2(connection, event);
    pool_commit();
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_read_event_callback2(struct http_server_connection *connection)
{
    if (connection->request.read_state == READ_END) {
        /* check if the connection was closed by the client while we
           were processing the request */

        if (fifo_buffer_full(connection->input)) {
            /* the buffer is full, the peer has been pipelining too
               much - that would disallow us to detect a disconnect;
               let's disable keep-alive now and discard all data */
            connection->keep_alive = false;
            fifo_buffer_clear(connection->input);
        }

        if (!http_server_read_to_buffer(connection))
            /* client has disconnected */
            return false;

        /* read more (no need to reschedule due to EV_PERSIST) */
        return true;
    }

    return http_server_try_read(connection);
}

static void
http_server_read_event_callback(gcc_unused int fd, gcc_unused short event, void *ctx)
{
    struct http_server_connection *connection = ctx;

    http_server_read_event_callback2(connection);
    pool_commit();
}

static void
http_server_timeout_callback(int fd gcc_unused, short event gcc_unused,
                             void *ctx)
{
    struct http_server_connection *connection = ctx;

    daemon_log(4, "%s timeout on HTTP connection from %s\n",
               connection->request.read_state == READ_START
               ? "idle"
               : (connection->request.read_state == READ_HEADERS
                  ? "header" : "read"),
               connection->remote_host);
    http_server_cancel(connection);
    pool_commit();
}

void
http_server_connection_new(struct pool *pool, int fd, enum istream_direct fd_type,
                           const struct sockaddr *local_address,
                           size_t local_address_length,
                           const struct sockaddr *remote_address,
                           size_t remote_address_length,
                           bool date_header,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           struct http_server_connection **connection_r)
{
    struct http_server_connection *connection;

    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->request != NULL);
    assert(handler->error != NULL);
    assert(handler->free != NULL);
    assert((local_address == NULL) == (local_address_length == 0));

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    connection->fd_type = fd_type,
    connection->handler = handler;
    connection->handler_ctx = ctx;
    connection->local_address = local_address != NULL
        ? (const struct sockaddr *)p_memdup(pool, local_address,
                                            local_address_length)
        : NULL;
    connection->local_address_length = local_address_length;
    connection->local_host = local_address != NULL
        ? address_to_string(pool, local_address, local_address_length)
        : NULL;
    connection->remote_address = remote_address != NULL
        ? address_to_string(pool, remote_address, remote_address_length)
        : NULL;
    connection->remote_host = remote_address != NULL
        ? address_to_host_string(pool, remote_address, remote_address_length)
        : NULL;
    connection->date_header = date_header;
    connection->request.read_state = READ_START;
    connection->request.request = NULL;
    connection->request.bytes_received = 0;
    connection->response.istream = NULL;
    connection->response.bytes_sent = 0;

    connection->input = fifo_buffer_new(pool, 4096);

    event_set(&connection->request.event, connection->fd,
              EV_READ|EV_PERSIST,
              http_server_read_event_callback, connection);
    event_add(&connection->request.event, NULL);

    event_set(&connection->response.event, connection->fd,
              EV_WRITE|EV_PERSIST|EV_TIMEOUT,
              http_server_write_event_callback, connection);

    evtimer_set(&connection->timeout,
                http_server_timeout_callback, connection);
    evtimer_add(&connection->timeout, &http_server_idle_timeout);

    connection->score = HTTP_SERVER_NEW;

    *connection_r = connection;

    http_server_try_read(connection);
}

static void
http_server_socket_close(struct http_server_connection *connection)
{
    assert(connection->fd >= 0);

    event_del(&connection->request.event);
    event_del(&connection->response.event);

    close(connection->fd);
    connection->fd = -1;

    evtimer_del(&connection->timeout);
}

static void
http_server_request_close(struct http_server_connection *connection)
{
    struct pool *pool;

    assert(connection->request.read_state != READ_START);
    assert(connection->request.request != NULL);

    pool = connection->request.request->pool;
    pool_trash(pool);
    pool_unref(pool);
    connection->request.request = NULL;

    if ((connection->request.read_state == READ_BODY ||
         connection->request.read_state == READ_END) &&
        (connection->response.writing_100_continue ||
         connection->response.istream == NULL) &&
        async_ref_defined(&connection->request.async_ref))
        async_abort(&connection->request.async_ref);

    if (connection->request.read_state == READ_BODY) {
        connection->request.read_state = READ_START;
        GError *error =
            g_error_new_literal(http_server_quark(), 0,
                                "connection closed");
        istream_deinit_abort(&connection->request.body_reader.output, error);
    } else
        connection->request.read_state = READ_START;

    if (connection->response.istream != NULL)
        istream_free_handler(&connection->response.istream);
}

void
http_server_done(struct http_server_connection *connection)
{
    assert(connection != NULL);
    assert(connection->handler != NULL);
    assert(connection->handler->free != NULL);
    assert(connection->request.read_state == READ_START);

    if (connection->fd >= 0)
        http_server_socket_close(connection);

    const struct http_server_connection_handler *handler = connection->handler;
    connection->handler = NULL;

    handler->free(connection->handler_ctx);
}

void
http_server_cancel(struct http_server_connection *connection)
{
    assert(connection != NULL);
    assert(connection->handler != NULL);
    assert(connection->handler->free != NULL);

    if (connection->fd >= 0)
        http_server_socket_close(connection);

    pool_ref(connection->pool);

    if (connection->request.read_state != READ_START)
        http_server_request_close(connection);

    if (connection->handler != NULL) {
        connection->handler->free(connection->handler_ctx);
        connection->handler = NULL;
    }

    pool_unref(connection->pool);
}

void
http_server_error(struct http_server_connection *connection, GError *error)
{
    assert(connection != NULL);
    assert(connection->handler != NULL);
    assert(connection->handler->free != NULL);

    if (connection->fd >= 0)
        http_server_socket_close(connection);

    pool_ref(connection->pool);

    if (connection->request.read_state != READ_START)
        http_server_request_close(connection);

    if (connection->handler != NULL) {
        const struct http_server_connection_handler *handler = connection->handler;
        void *handler_ctx = connection->handler_ctx;
        connection->handler = NULL;
        connection->handler_ctx = NULL;
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
    assert(connection != NULL);

    if (connection->fd >= 0)
        http_server_socket_close(connection);

    connection->handler = NULL;

    if (connection->request.read_state != READ_START)
        http_server_request_close(connection);
}

void
http_server_errno(struct http_server_connection *connection, const char *msg)
{
    GError *error = g_error_new(g_file_error_quark(), errno,
                                "%s: %s", msg, strerror(errno));
    http_server_error(connection, error);
}

void
http_server_connection_graceful(struct http_server_connection *connection)
{
    assert(connection != NULL);

    if (connection->request.read_state == READ_START)
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
