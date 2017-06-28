/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "direct.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

size_t
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

    SocketErrorErrno("write error on HTTP connection");
    return 0;
}

ssize_t
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

void
HttpServerConnection::OnEof()
{
    assert(request.read_state != Request::START &&
           request.read_state != Request::HEADERS);
    assert(request.request != nullptr);
    assert(response.istream.IsDefined());
    assert(!response.pending_drained);

    response.istream.Clear();

    ResponseIstreamFinished();
}

void
HttpServerConnection::OnError(std::exception_ptr ep)
{
    assert(response.istream.IsDefined());

    response.istream.Clear();

    /* we clear this cancel_ptr here so http_server_request_close()
       won't think we havn't sent a response yet */
    request.cancel_ptr = nullptr;

    Error(NestException(ep,
                        std::runtime_error("error on HTTP response stream")));
}

void
HttpServerConnection::SetResponseIstream(Istream &r)
{
    response.istream.Set(r, *this, istream_direct_mask_to(socket.GetType()));
}

bool
HttpServerConnection::ResponseIstreamFinished()
{
    socket.UnscheduleWrite();

    Log();

    /* check for end of chunked request body again, just in case
       DechunkIstream has announced this in a derred event */
    if (request.read_state == Request::BODY && request_body_reader->IsEOF()) {
        request.read_state = Request::END;
        request_body_reader->DestroyEof();
        if (!IsValid())
            return false;
    }

    if (request.read_state == Request::BODY &&
        !request.expect_100_continue) {
        /* We are still reading the request body, which we don't need
           anymore.  To discard it, we simply close the connection by
           disabling keepalive; this seems cheaper than redirecting
           the rest of the body to /dev/null */
        keep_alive = false;
        request.read_state = Request::END;

        request_body_reader->DestroyError(std::make_exception_ptr(std::runtime_error("request body discarded")));
        if (!IsValid())
            return false;
    }

    pool_trash(&request.request->pool);
    pool_unref(&request.request->pool);
    request.request = nullptr;
    request.bytes_received = 0;
    response.bytes_sent = 0;

    request.read_state = Request::START;

    if (keep_alive) {
        /* handle pipelined request (if any), or set up events for
           next request */

        socket.ScheduleReadNoTimeout(false);
        idle_timeout.Add(http_server_idle_timeout);

        return true;
    } else {
        /* keepalive disabled and response is finished: we must close
           the connection */

        if (socket.IsDrained()) {
            Done();
            return false;
        } else {
            /* there is still data in the filter's output buffer; wait for
               that to drain, which will trigger
               http_server_socket_drained() */
            assert(!response.pending_drained);

            response.pending_drained = true;

            return true;
        }
    }
}
