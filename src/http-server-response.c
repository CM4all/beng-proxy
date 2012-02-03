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

    assert(socket_wrapper_valid(&connection->socket) ||
           connection->request.request == NULL);
    assert(connection->response.istream != NULL);

    if (!socket_wrapper_valid(&connection->socket))
        return 0;

    nbytes = socket_wrapper_write(&connection->socket, data, length);

    if (likely(nbytes >= 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        http_server_schedule_write(connection);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        http_server_schedule_write(connection);
        return 0;
    }

    if (errno == ECONNRESET)
        http_server_cancel(connection);
    else
        http_server_errno(connection, "write error on HTTP connection");

    return 0;
}

#ifdef __linux
static ssize_t
http_server_response_stream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct http_server_connection *connection = ctx;
    ssize_t nbytes;

    assert(socket_wrapper_valid(&connection->socket) ||
           connection->request.request == NULL);
    assert(connection->response.istream != NULL);

    if (!socket_wrapper_valid(&connection->socket))
        return 0;

    nbytes = socket_wrapper_write_from(&connection->socket, fd, type,
                                       max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!socket_wrapper_ready_for_writing(&connection->socket)) {
            http_server_schedule_write(connection);
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = socket_wrapper_write_from(&connection->socket, fd, type,
                                           max_length);
    }

    if (likely(nbytes > 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        http_server_schedule_write(connection);
    }

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

    socket_wrapper_unschedule_write(&connection->socket);

    if (connection->response.writing_100_continue)
        /* connection->response.istream contained the string "100
           Continue", and not a full response - return here, because
           we do not want the request/response pair to be
           destructed */
        return;

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

        http_server_consume_input(connection);
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
