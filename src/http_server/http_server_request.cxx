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
#include "pool.hxx"

BufferedResult
HttpServerConnection::FeedRequestBody(const void *data, size_t length)
{
    assert(request.read_state == Request::BODY);
    assert(request.body_state == Request::BodyState::READING);
    assert(!response.pending_drained);

    const ScopePoolRef ref(*pool TRACE_ARGS);

    size_t nbytes = request_body_reader->FeedBody(data, length);
    if (nbytes == 0) {
        return socket.IsValid()
            ? BufferedResult::BLOCKING
            : BufferedResult::CLOSED;
    }

    request.bytes_received += nbytes;
    socket.Consumed(nbytes);

    if (request.read_state == Request::BODY && request_body_reader->IsEOF()) {
        request.read_state = Request::END;
#ifndef NDEBUG
        request.body_state = Request::BodyState::CLOSED;
#endif

        /* re-enable the event, to detect client disconnect while
           we're processing the request */
        socket.ScheduleReadNoTimeout(false);

        request_body_reader->DestroyEof();
        if (!IsValid())
            return BufferedResult::CLOSED;
    }

    return nbytes == length
        ? BufferedResult::OK
        : BufferedResult::PARTIAL;
}

off_t
HttpServerConnection::RequestBodyReader::_GetAvailable(bool partial)
{
    assert(connection.IsValid());
    assert(connection.request.read_state == Request::BODY);
    assert(connection.request.body_state == Request::BodyState::READING);
    assert(!connection.response.pending_drained);

    return HttpBodyReader::GetAvailable(connection.socket, partial);
}

void
HttpServerConnection::RequestBodyReader::_Read()
{
    assert(connection.IsValid());
    assert(connection.request.read_state == Request::BODY);
    assert(connection.request.body_state == Request::BodyState::READING);
    assert(!connection.response.pending_drained);

    if (!connection.MaybeSend100Continue())
        return;

    if (connection.request.in_handler)
        /* avoid recursion */
        return;

    connection.socket.Read(connection.request_body_reader->RequireMore());
}

void
HttpServerConnection::RequestBodyReader::_Close() noexcept
{
    if (connection.request.read_state == Request::END)
        return;

    assert(connection.request.read_state == Request::BODY);
    assert(connection.request.body_state == Request::BodyState::READING);
    assert(!connection.request_body_reader->IsEOF());
    assert(!connection.response.pending_drained);

    if (!connection.socket.IsValid() ||
        !connection.socket.IsConnected()) {
        /* this happens when there's an error on the socket while
           reading the request body before the response gets
           submitted, and this HTTP server library invokes the
           handler's abort method; the handler will free the request
           body, but the socket is already closed */
        assert(connection.request.request == nullptr);
    }

    connection.request.read_state = Request::END;
#ifndef NDEBUG
    connection.request.body_state = Request::BodyState::CLOSED;
#endif

    if (connection.request.expect_100_continue)
        /* the request body was optional, and we did not send the "100
           Continue" response (yet): pretend there never was a request
           body */
        connection.request.expect_100_continue = false;
    else
        /* disable keep-alive so we don't need to wait for the client
           to finish sending the request body */
        connection.keep_alive = false;

    connection.request_body_reader->Destroy();
}
