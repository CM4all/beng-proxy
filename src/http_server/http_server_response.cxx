/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "direct.hxx"
#include "istream/istream_oo.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

inline size_t
HttpServerConnection::OnData(const void *data, size_t length)
{
    assert(socket.IsConnected() || request.request == nullptr);
    assert(response.istream.IsDefined());
    assert(!response.pending_drained);

    if (!socket.IsConnected())
        return 0;

    ssize_t nbytes = socket.Write(data, length);

    if (likely(nbytes >= 0)) {
        response.bytes_sent += nbytes;
        response.length += (off_t)nbytes;
        ScheduleWrite();
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING)) {
        response.want_write = true;
        return 0;
    }

    if (nbytes == WRITE_DESTROYED)
        return 0;

    ErrorErrno("write error on HTTP connection");
    return 0;
}

inline ssize_t
HttpServerConnection::OnDirect(FdType type, int fd, size_t max_length)
{
    assert(socket.IsConnected() || request.request == nullptr);
    assert(response.istream.IsDefined());
    assert(!response.pending_drained);

    if (!socket.IsConnected())
        return 0;

    ssize_t nbytes = socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0)) {
        response.bytes_sent += nbytes;
        response.length += (off_t)nbytes;
        ScheduleWrite();
    } else if (nbytes == WRITE_BLOCKING) {
        response.want_write = true;
        return ISTREAM_RESULT_BLOCKING;
    } else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;

    return nbytes;
}

inline void
HttpServerConnection::OnEof()
{
    assert(request.read_state != Request::START &&
           request.read_state != Request::HEADERS);
    assert(request.request != nullptr);
    assert(response.istream.IsDefined());
    assert(!response.pending_drained);

    response.istream.Clear();

    socket.UnscheduleWrite();

    Log();

    if (request.read_state == Request::BODY &&
        !request.expect_100_continue) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        keep_alive = false;
        request.read_state = Request::END;

        GError *error =
            g_error_new_literal(http_server_quark(), 0,
                                "request body discarded");
        request_body_reader->DeinitAbort(error);
        if (!IsValid())
            return;
    }

    pool_trash(request.request->pool);
    pool_unref(request.request->pool);
    request.request = nullptr;
    request.bytes_received = 0;
    response.bytes_sent = 0;

    request.read_state = Request::START;

    if (keep_alive) {
        /* handle pipelined request (if any), or set up events for
           next request */

        socket.ScheduleReadNoTimeout(false);
        evtimer_add(&idle_timeout, &http_server_idle_timeout);
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */

        if (socket.IsDrained()) {
            Done();
        } else {
            /* there is still data in the filter's output buffer; wait for
               that to drain, which will trigger
               http_server_socket_drained() */
            assert(!response.pending_drained);

            response.pending_drained = true;
        }
    }
}

inline void
HttpServerConnection::OnError(GError *error)
{
    assert(response.istream.IsDefined());

    response.istream.Clear();

    /* we clear this async_ref here so http_server_request_close()
       won't think we havn't sent a response yet */
    request.async_ref.Clear();

    g_prefix_error(&error, "error on HTTP response stream: ");
    Error(error);
}

void
HttpServerConnection::SetResponseIstream(struct istream &r)
{
    response.istream.Set(r,
                         MakeIstreamHandler<HttpServerConnection>::handler, this,
                         socket.GetDirectMask());
}

