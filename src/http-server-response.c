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

#include <unistd.h>
#include <errno.h>
#include <string.h>

static size_t
http_server_response_stream_data(const void *data, size_t length, void *ctx)
{
    http_server_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->response.istream != NULL);

    nbytes = write(connection->fd, data, length);

    if (likely(nbytes >= 0)) {
        connection->response.length += (off_t)nbytes;
        event2_or(&connection->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&connection->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "write error on HTTP connection: %s\n", strerror(errno));
    http_server_connection_close(connection);
    return 0;
}

#ifdef __linux
static ssize_t
http_server_response_stream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    http_server_connection_t connection = ctx;
    ssize_t nbytes;

    assert(connection->response.istream != NULL);

    nbytes = istream_direct_to_socket(type, fd, connection->fd, max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd_ready_for_writing(connection->fd)) {
            event2_or(&connection->event, EV_WRITE);
            return -2;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to_socket(type, fd, connection->fd, max_length);
    }

    if (likely(nbytes > 0)) {
        connection->response.length += (off_t)nbytes;
        event2_or(&connection->event, EV_WRITE);
    }

    return nbytes;
}
#endif

static void
http_server_response_stream_eof(void *ctx)
{
    http_server_connection_t connection = ctx;

    assert(connection->request.read_state != READ_START &&
           connection->request.read_state != READ_HEADERS);
    assert(connection->request.request != NULL);
    assert(connection->response.istream != NULL);

    connection->response.istream = NULL;

    event2_nand(&connection->event, EV_WRITE);

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
                                 connection->handler_ctx);

    if (connection->request.read_state == READ_BODY &&
        !connection->request.expect_100_continue) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        connection->keep_alive = false;
        connection->request.read_state = READ_END;

        istream_deinit_abort(&connection->request.body_reader.output);
        if (!http_server_connection_valid(connection))
            return;
    }

    pool_unref(connection->request.request->pool);
    connection->request.request = NULL;

    connection->request.read_state = READ_START;

    if (connection->keep_alive) {
        /* handle pipelined request (if any), or set up events for
           next request */

        http_server_consume_input(connection);
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */
        http_server_connection_close(connection);
    }
}

static void
http_server_response_stream_abort(void *ctx)
{
    http_server_connection_t connection = ctx;

    assert(connection->response.istream != NULL);

    connection->response.istream = NULL;

    /* we clear this async_ref here so http_server_request_close()
       won't think we havn't sent a response yet */
    async_ref_clear(&connection->request.async_ref);

    http_server_connection_close(connection);
}

const struct istream_handler http_server_response_stream_handler = {
    .data = http_server_response_stream_data,
#ifdef __linux
    .direct = http_server_response_stream_direct,
#endif
    .eof = http_server_response_stream_eof,
    .abort = http_server_response_stream_abort,
};
