/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "istream-internal.h"

bool
http_server_consume_body(struct http_server_connection *connection)
{
    size_t nbytes;

    assert(connection != NULL);
    assert(connection->request.read_state == READ_BODY);
    assert(connection->request.request->body != NULL);

    /* checking request.request->body and not request.body_reader,
       because the dechunker might be attached to the
       http_body_reader */
    if (!istream_has_handler(connection->request.request->body))
        /* the handler is not yet connected */
        return false;

    nbytes = http_body_consume_body(&connection->request.body_reader, connection->input);
    if (nbytes == 0)
        return false;

    assert(!fifo_buffer_full(connection->input));

    if (connection->request.read_state == READ_BODY &&
        http_body_eof(&connection->request.body_reader)) {
        connection->request.read_state = READ_END;
        istream_deinit_eof(&connection->request.body_reader.output);
        if (!http_server_connection_valid(connection))
            return false;
    }

    return true;
}

static inline struct http_server_connection *
response_stream_to_connection(istream_t istream)
{
    return (struct http_server_connection *)(((char*)istream) - offsetof(struct http_server_connection, request.body_reader.output));
}

static off_t
http_server_request_stream_available(istream_t istream, bool partial)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);

    return http_body_available(&connection->request.body_reader,
                               connection->input, partial);
}

static void
http_server_request_stream_read(istream_t istream)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);
    assert(istream_has_handler(http_body_istream(&connection->request.body_reader)));
    assert(connection->request.request->body != NULL);
    assert(istream_has_handler(connection->request.request->body));

    pool_ref(connection->pool);

    if (http_server_consume_body(connection) &&
        connection->request.read_state == READ_BODY)
        http_server_try_read(connection);

    pool_unref(connection->pool);
}

static void
http_server_request_stream_close(istream_t istream)
{
    struct http_server_connection *connection = response_stream_to_connection(istream);

    if (connection->request.read_state == READ_END)
        return;

    assert(connection->request.read_state == READ_BODY);
    assert(!http_body_eof(&connection->request.body_reader));

    event2_nand(&connection->event, EV_READ);

    connection->request.read_state = READ_END;

    if (connection->request.request != NULL)
        connection->request.request->body = NULL;

    connection->keep_alive = false;

    istream_deinit(&connection->request.body_reader.output);
}

const struct istream http_server_request_stream = {
    .available = http_server_request_stream_available,
    .read = http_server_request_stream_read,
    .close = http_server_request_stream_close,
};
