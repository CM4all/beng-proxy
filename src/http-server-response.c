/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "direct.h"
#include "istream-internal.h"
#include "fd-util.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

static size_t
http_server_response_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_server_connection *connection = ctx;
    ssize_t nbytes;

    assert(filtered_socket_connected(&connection->socket) ||
           connection->request.request == NULL);
    assert(connection->response.istream != NULL);

    if (!filtered_socket_connected(&connection->socket))
        return 0;

    nbytes = filtered_socket_write(&connection->socket, data, length);

    if (likely(nbytes >= 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        http_server_schedule_write(connection);
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING))
        return 0;

    http_server_errno(connection, "write error on HTTP connection");
    return 0;
}

#ifdef __linux
static ssize_t
http_server_response_stream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct http_server_connection *connection = ctx;
    ssize_t nbytes;

    assert(filtered_socket_connected(&connection->socket) ||
           connection->request.request == NULL);
    assert(connection->response.istream != NULL);

    if (!filtered_socket_connected(&connection->socket))
        return 0;

    nbytes = filtered_socket_write_from(&connection->socket, fd, type,
                                        max_length);
    if (likely(nbytes > 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        http_server_schedule_write(connection);
    } else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;

    return nbytes;
}
#endif

static void
http_server_response_stream_eof(void *ctx)
{
    struct http_server_connection *connection = ctx;

    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.istream != NULL);

    connection->response.istream = NULL;

    filtered_socket_unschedule_write(&connection->socket);

    if (connection->handler->log != NULL)
        connection->handler->log(connection->request.request,
                                 connection->response.status,
                                 connection->response.length,
                                 connection->request.bytes_received,
                                 connection->response.bytes_sent,
                                 connection->handler_ctx);

    if (connection->request.read_state == READ_BODY &&
        !connection->request.expect_100_continue) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        connection->keep_alive = false;
        connection->request.read_state = READ_END;

        GError *error =
            g_error_new_literal(http_server_quark(), 0,
                                "request body discarded");
        istream_deinit_abort(&connection->request.body_reader.output, error);
        if (!http_server_connection_valid(connection))
            return;
    }

    pool_trash(connection->request.request->pool);
    pool_unref(connection->request.request->pool);
    connection->request.request = NULL;
    connection->request.bytes_received = 0;
    connection->response.bytes_sent = 0;

    connection->request.read_state = READ_START;

    if (connection->keep_alive) {
        /* handle pipelined request (if any), or set up events for
           next request */

        filtered_socket_schedule_read_no_timeout(&connection->socket, false);
        evtimer_add(&connection->idle_timeout, &http_server_idle_timeout);
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */
        http_server_done(connection);
    }
}

static void
http_server_response_stream_abort(GError *error, void *ctx)
{
    struct http_server_connection *connection = ctx;

    assert(connection->response.istream != NULL);

    connection->response.istream = NULL;

    /* we clear this async_ref here so http_server_request_close()
       won't think we havn't sent a response yet */
    async_ref_clear(&connection->request.async_ref);

    g_prefix_error(&error, "error on HTTP response stream: ");
    http_server_error(connection, error);
}

const struct istream_handler http_server_response_stream_handler = {
    .data = http_server_response_stream_data,
#ifdef __linux
    .direct = http_server_response_stream_direct,
#endif
    .eof = http_server_response_stream_eof,
    .abort = http_server_response_stream_abort,
};
