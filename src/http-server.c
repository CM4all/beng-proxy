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

struct http_server_request *
http_server_request_new(struct http_server_connection *connection)
{
    pool_t pool;
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

    event2_lock(&connection->event);
    event2_nand(&connection->event, EV_WRITE);

    pool_ref(connection->pool);
    istream_read(connection->response.istream);

    if (!http_server_connection_valid(connection)) {
        pool_unref(connection->pool);
        return false;
    }

    pool_unref(connection->pool);

    event2_unlock(&connection->event);
    return true;
}

/**
 * @return false if the connection has been closed
 */
static bool
http_server_event_callback2(struct http_server_connection *connection,
                            short event)
{
    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        http_server_connection_close(connection);
        return false;
    }

    if ((event & EV_WRITE) != 0 &&
        !http_server_try_write(connection))
        return false;

    if ((event & EV_READ) != 0 &&
        connection->request.read_state == READ_END) {
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

        /* read more */
        event2_or(&connection->event, EV_READ);
        return true;
    }

    if ((event & EV_READ) != 0) {
        pool_ref(connection->pool);
        http_server_try_read(connection);
        if (!http_server_connection_valid(connection)) {
            pool_unref(connection->pool);
            return false;
        }

        pool_unref(connection->pool);
    }

    return true;
}

static void
http_server_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct http_server_connection *connection = ctx;

    event2_lock(&connection->event);
    event2_occurred_persist(&connection->event, event);

    if (http_server_event_callback2(connection, event))
        event2_unlock(&connection->event);

    pool_commit();
}

static void
http_server_timeout_callback(int fd __attr_unused, short event __attr_unused,
                             void *ctx)
{
    struct http_server_connection *connection = ctx;

    daemon_log(4, "header timeout\n");
    http_server_connection_close(connection);
}

void
http_server_connection_new(pool_t pool, int fd, enum istream_direct fd_type,
                           const struct sockaddr *local_address,
                           size_t local_address_length,
                           const char *remote_host,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           struct http_server_connection **connection_r)
{
    struct http_server_connection *connection;
    static const struct timeval tv = {
        .tv_sec = 30,
        .tv_usec = 0,
    };

    assert(fd >= 0);
    assert(handler != NULL);
    assert(handler->request != NULL);
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
    connection->remote_host = remote_host;
    connection->request.read_state = READ_START;
    connection->request.request = NULL;
    connection->request.bytes_received = 0;
    connection->response.istream = NULL;
    connection->response.bytes_sent = 0;

    connection->input = fifo_buffer_new(pool, 4096);

    pool_ref(connection->pool);

    event2_init(&connection->event, connection->fd,
                http_server_event_callback, connection,
                &tv);
    event2_persist(&connection->event);
    event2_lock(&connection->event);

    evtimer_set(&connection->timeout,
                http_server_timeout_callback, connection);

    connection->score = HTTP_SERVER_NEW;

    *connection_r = connection;

    http_server_try_read(connection);

    event2_unlock(&connection->event);
    pool_unref(connection->pool);
}

static void
http_server_socket_close(struct http_server_connection *connection)
{
    assert(connection->fd >= 0);

    event2_set(&connection->event, 0);
    event2_commit(&connection->event);
    close(connection->fd);
    connection->fd = -1;

    evtimer_del(&connection->timeout);
}

static void
http_server_request_close(struct http_server_connection *connection)
{
    pool_t pool;

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
http_server_connection_close(struct http_server_connection *connection)
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
http_server_connection_graceful(struct http_server_connection *connection)
{
    assert(connection != NULL);

    if (connection->request.read_state == READ_START)
        /* there is no request currently; close the connection
           immediately */
        http_server_connection_close(connection);
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
