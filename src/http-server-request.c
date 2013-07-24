/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "istream-internal.h"

enum buffered_result
http_server_feed_body(struct http_server_connection *connection,
                      const void *data, size_t length)
{
    assert(connection != NULL);
    assert(connection->request.read_state == READ_BODY);
    assert(connection->request.request->body != NULL);

    /* checking request.request->body and not request.body_reader,
       because the dechunker might be attached to the
       http_body_reader */
    if (!istream_has_handler(connection->request.request->body))
        /* the handler is not yet connected */
        return BUFFERED_BLOCKING;

    struct pool *pool = connection->pool;
    pool_ref(pool);

    size_t nbytes = http_body_feed_body(&connection->request.body_reader,
                                        data, length);
    if (nbytes == 0) {
        const bool valid = filtered_socket_valid(&connection->socket);
        pool_unref(pool);
        return valid
            ? BUFFERED_BLOCKING
            : BUFFERED_CLOSED;
    }

    pool_unref(pool);

    connection->request.bytes_received += nbytes;
    filtered_socket_consumed(&connection->socket, nbytes);

    if (connection->request.read_state == READ_BODY &&
        http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;

        /* re-enable the event, to detect client disconnect while
           we're processing the request */
        filtered_socket_schedule_read_no_timeout(&connection->socket, false);

        pool_ref(connection->pool);
        istream_deinit_eof(&connection->request.body_reader.output);
        const bool valid = http_server_connection_valid(connection);
        pool_unref(connection->pool);

        if (!valid)
            return BUFFERED_CLOSED;
    }

    return nbytes == length
        ? BUFFERED_OK
        : BUFFERED_PARTIAL;
}

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wextended-offsetof"
#endif

static inline struct http_server_connection *
response_stream_to_connection(struct istream *istream)
{
    return (struct http_server_connection *)(((char*)istream) - offsetof(struct http_server_connection, request.body_reader.output));
}

static off_t
http_server_request_stream_available(struct istream *istream, bool partial)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state == READ_BODY);

    return http_body_available(&connection->request.body_reader,
                               &connection->socket, partial);
}

static void
http_server_request_stream_read(struct istream *istream)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    assert(http_server_connection_valid(connection));
    assert(connection->request.read_state == READ_BODY);
    assert(istream_has_handler(http_body_istream(&connection->request.body_reader)));
    assert(connection->request.request->body != NULL);
    assert(istream_has_handler(connection->request.request->body));

    if (connection->request.in_handler)
        /* avoid recursion */
        return;

    if (!http_server_maybe_send_100_continue(connection))
        return;

    filtered_socket_read(&connection->socket,
                         http_body_require_more(&connection->request.body_reader));
}

static void
http_server_request_stream_close(struct istream *istream)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    if (connection->request.read_state == READ_END)
        return;

    assert(connection->request.read_state == READ_BODY);
    assert(!http_body_eof(&connection->request.body_reader));

    if (!filtered_socket_valid(&connection->socket) ||
        !filtered_socket_connected(&connection->socket)) {
        /* this happens when there's an error on the socket while
           reading the request body before the response gets
           submitted, and this HTTP server library invokes the
           handler's abort method; the handler will free the request
           body, but the socket is already closed */
        assert(connection->request.request == NULL);
    }

    connection->request.read_state = READ_END;

    if (connection->request.request != NULL)
        connection->request.request->body = NULL;

    connection->keep_alive = false;

    istream_deinit(&connection->request.body_reader.output);
}

const struct istream_class http_server_request_stream = {
    .available = http_server_request_stream_available,
    .read = http_server_request_stream_read,
    .close = http_server_request_stream_close,
};
