/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Internal.hxx"
#include "Request.hxx"
#include "pool.hxx"

BufferedResult
HttpServerConnection::FeedRequestBody(const void *data, size_t length)
{
    assert(request.read_state == Request::BODY);
    assert(request.request->body != nullptr);
    assert(!response.pending_drained);

    /* checking request.request->body and not request_body_reader,
       because the dechunker might be attached to the
       http_body_reader */
    if (!istream_has_handler(request.request->body))
        /* the handler is not yet connected */
        return BufferedResult::BLOCKING;

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
HttpServerConnection::RequestBodyReader::GetAvailable(bool partial)
{
    assert(connection.IsValid());
    assert(connection.request.read_state == Request::BODY);
    assert(!connection.response.pending_drained);

    return HttpBodyReader::GetAvailable(connection.socket, partial);
}

void
HttpServerConnection::RequestBodyReader::Read()
{
    assert(connection.IsValid());
    assert(connection.request.read_state == Request::BODY);
    assert(istream_has_handler(connection.request_body_reader->Cast()));
    assert(connection.request.request->body != nullptr);
    assert(istream_has_handler(connection.request.request->body));
    assert(!connection.response.pending_drained);

    if (connection.request.in_handler)
        /* avoid recursion */
        return;

    if (!connection.MaybeSend100Continue())
        return;

    connection.socket.Read(connection.request_body_reader->RequireMore());
}

void
HttpServerConnection::RequestBodyReader::Close()
{
    if (connection.request.read_state == Request::END)
        return;

    assert(connection.request.read_state == Request::BODY);
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

    if (connection.request.request != nullptr)
        connection.request.request->body = nullptr;

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
