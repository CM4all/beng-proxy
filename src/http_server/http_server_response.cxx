/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
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

    if (gcc_likely(nbytes >= 0)) {
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
    if (gcc_likely(nbytes > 0)) {
        response.bytes_sent += nbytes;
        response.length += (off_t)nbytes;
        ScheduleWrite();
    } else if (nbytes == WRITE_BLOCKING) {
        response.want_write = true;
        return ISTREAM_RESULT_BLOCKING;
    } else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;
    else if (gcc_likely(nbytes < 0) && errno == EAGAIN)
        socket.UnscheduleWrite();

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
