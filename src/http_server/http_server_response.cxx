/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "direct.hxx"
#include "fd-util.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

static size_t
http_server_response_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(connection->socket.IsConnected() ||
           connection->request.request == nullptr);
    assert(connection->response.istream != nullptr);
    assert(!connection->response.pending_drained);

    if (!connection->socket.IsConnected())
        return 0;

    ssize_t nbytes = connection->socket.Write(data, length);

    if (likely(nbytes >= 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        connection->ScheduleWrite();
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING)) {
        connection->response.want_write = true;
        return 0;
    }

    if (nbytes == WRITE_DESTROYED)
        return 0;

    connection->ErrorErrno("write error on HTTP connection");
    return 0;
}

#ifdef __linux
static ssize_t
http_server_response_stream_direct(FdType type, int fd, size_t max_length,
                                   void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(connection->socket.IsConnected() ||
           connection->request.request == nullptr);
    assert(connection->response.istream != nullptr);
    assert(!connection->response.pending_drained);

    if (!connection->socket.IsConnected())
        return 0;

    ssize_t nbytes = connection->socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0)) {
        connection->response.bytes_sent += nbytes;
        connection->response.length += (off_t)nbytes;
        connection->ScheduleWrite();
    } else if (nbytes == WRITE_BLOCKING) {
        connection->response.want_write = true;
        return ISTREAM_RESULT_BLOCKING;
    } else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;

    return nbytes;
}
#endif

static void
http_server_response_stream_eof(void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(connection->request.read_state != http_server_connection::Request::START &&
           connection->request.read_state != http_server_connection::Request::HEADERS);
    assert(connection->request.request != nullptr);
    assert(connection->response.istream != nullptr);
    assert(!connection->response.pending_drained);

    connection->response.istream = nullptr;

    connection->socket.UnscheduleWrite();

    connection->Log();

    if (connection->request.read_state == http_server_connection::Request::BODY &&
        !connection->request.expect_100_continue) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        connection->keep_alive = false;
        connection->request.read_state = http_server_connection::Request::END;

        GError *error =
            g_error_new_literal(http_server_quark(), 0,
                                "request body discarded");
        connection->request_body_reader.DeinitAbort(error);
        if (!connection->IsValid())
            return;
    }

    pool_trash(connection->request.request->pool);
    pool_unref(connection->request.request->pool);
    connection->request.request = nullptr;
    connection->request.bytes_received = 0;
    connection->response.bytes_sent = 0;

    connection->request.read_state = http_server_connection::Request::START;

    if (connection->keep_alive) {
        /* handle pipelined request (if any), or set up events for
           next request */

        connection->socket.ScheduleReadNoTimeout(false);
        evtimer_add(&connection->idle_timeout, &http_server_idle_timeout);
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */

        if (connection->socket.IsDrained()) {
            connection->Done();
        } else {
            /* there is still data in the filter's output buffer; wait for
               that to drain, which will trigger
               http_server_socket_drained() */
            assert(!connection->response.pending_drained);

            connection->response.pending_drained = true;
        }
    }
}

static void
http_server_response_stream_abort(GError *error, void *ctx)
{
    struct http_server_connection *connection =
        (struct http_server_connection *)ctx;

    assert(connection->response.istream != nullptr);

    connection->response.istream = nullptr;

    /* we clear this async_ref here so http_server_request_close()
       won't think we havn't sent a response yet */
    connection->request.async_ref.Clear();

    g_prefix_error(&error, "error on HTTP response stream: ");
    connection->Error(error);
}

const struct istream_handler http_server_response_stream_handler = {
    .data = http_server_response_stream_data,
#ifdef __linux
    .direct = http_server_response_stream_direct,
#endif
    .eof = http_server_response_stream_eof,
    .abort = http_server_response_stream_abort,
};
