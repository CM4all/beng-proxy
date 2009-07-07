/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "strmap.h"

#include <inline/compiler.h>
#include <daemon/log.h>
#include <socket/util.h>

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
    request->remote_host = connection->remote_host;
    request->headers = strmap_new(pool, 64);

    return request;
}

static inline void
http_server_cork(struct http_server_connection *connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);

#ifdef __linux
    if (!connection->cork) {
        connection->cork = true;
        socket_set_cork(connection->fd, connection->cork);
    }
#endif
}

static inline void
http_server_uncork(struct http_server_connection *connection)
{
    assert(connection != NULL);

#ifdef __linux
    if (connection->cork) {
        assert(connection->fd >= 0);
        connection->cork = false;
        socket_set_cork(connection->fd, connection->cork);
    }
#endif
}


void
http_server_try_write(struct http_server_connection *connection)
{
    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.istream != NULL);

    http_server_cork(connection);
    event2_lock(&connection->event);
    istream_read(connection->response.istream);
    event2_unlock(&connection->event);
    http_server_uncork(connection);
}

static void
http_server_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct http_server_connection *connection = ctx;

    pool_ref(connection->pool);

    event2_reset(&connection->event);
    event2_lock(&connection->event);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        http_server_connection_close(connection);
    }

    if (http_server_connection_valid(connection) && (event & EV_WRITE) != 0)
        http_server_try_write(connection);

    if (http_server_connection_valid(connection) && (event & EV_READ) != 0)
        http_server_try_read(connection);

    event2_unlock(&connection->event);

    pool_unref(connection->pool);
    pool_commit();
}

void
http_server_connection_new(pool_t pool, int fd,
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
    assert((local_address == NULL) == (local_address_length == 0));

    connection = p_malloc(pool, sizeof(*connection));
    connection->pool = pool;
    connection->fd = fd;
    connection->handler = handler;
    connection->handler_ctx = ctx;
    connection->local_address = local_address != NULL
        ? (const struct sockaddr *)p_memdup(pool, local_address,
                                            local_address_length)
        : NULL;
    connection->local_address_length = local_address_length;
    connection->remote_host = remote_host;
    connection->request.read_state = READ_START;
    connection->request.request = NULL;
    connection->response.istream = NULL;
#ifdef __linux
    connection->cork = false;
#endif

    connection->input = fifo_buffer_new(pool, 4096);

    pool_ref(connection->pool);

    event2_init(&connection->event, connection->fd,
                http_server_event_callback, connection,
                &tv);
    event2_lock(&connection->event);

    *connection_r = connection;

    http_server_try_read(connection);

    event2_unlock(&connection->event);
    pool_unref(connection->pool);
}

static void
http_server_request_close(struct http_server_connection *connection)
{
    pool_t pool;

    assert(connection->request.read_state != READ_START);
    assert(connection->request.request != NULL);

    pool = connection->request.request->pool;
    pool_unref(pool);
    pool_trash(pool);
    connection->request.request = NULL;

    if ((connection->request.read_state == READ_BODY ||
         connection->request.read_state == READ_END) &&
        (connection->response.writing_100_continue ||
         connection->response.istream == NULL) &&
        async_ref_defined(&connection->request.async_ref))
        async_abort(&connection->request.async_ref);

    if (connection->request.read_state == READ_BODY) {
        connection->request.read_state = READ_START;
        istream_deinit_abort(&connection->request.body_reader.output);
    } else
        connection->request.read_state = READ_START;

    if (connection->response.istream != NULL)
        istream_free_handler(&connection->response.istream);
}

void
http_server_connection_close(struct http_server_connection *connection)
{
    assert(connection != NULL);

    if (connection->fd >= 0) {
        event2_set(&connection->event, 0);
        close(connection->fd);
        connection->fd = -1;
    }

#ifdef __linux
    connection->cork = false;
#endif

    pool_ref(connection->pool);

    if (connection->request.read_state != READ_START)
        http_server_request_close(connection);

    if (connection->handler != NULL && connection->handler->free != NULL) {
        const struct http_server_connection_handler *handler = connection->handler;
        void *handler_ctx = connection->handler_ctx;
        connection->handler = NULL;
        connection->handler_ctx = NULL;
        handler->free(handler_ctx);
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
