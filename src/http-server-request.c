/*
 * HTTP server implementation.
 *
 * istream implementation for the request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-server-internal.h"
#include "direct.h"

void
http_server_consume_body(http_server_connection_t connection)
{
    assert(connection != NULL);
    assert(connection->request.read_state == READ_BODY);

    if (!istream_has_handler(http_body_istream(&connection->request.body_reader)))
        /* the handler is not yet connected */
        return;

    http_body_consume_body(&connection->request.body_reader, connection->input);

    if (!http_server_connection_valid(connection))
        return;

    event2_setbit(&connection->event, EV_READ, !fifo_buffer_full(connection->input));
}

static inline http_server_connection_t
response_stream_to_connection(istream_t istream)
{
    return (http_server_connection_t)(((char*)istream) - offsetof(struct http_server_connection, request.body_reader.output));
}

static void
http_server_request_stream_read(istream_t istream)
{
    http_server_connection_t connection = response_stream_to_connection(istream);

    assert(connection != NULL);
    assert(connection->fd >= 0);
    assert(connection->request.read_state == READ_BODY);
    assert(istream_has_handler(http_body_istream(&connection->request.body_reader)));

    pool_ref(connection->pool);

    http_server_consume_body(connection);

    if (connection->request.read_state == READ_BODY)
        http_server_try_read(connection);

    pool_unref(connection->pool);
}

static void
http_server_request_stream_close(istream_t istream)
{
    http_server_connection_t connection = response_stream_to_connection(istream);

    if (connection->request.read_state == READ_END)
        return;

    assert(connection->request.read_state == READ_BODY);
    assert(!http_body_eof(&connection->request.body_reader));

    event2_nand(&connection->event, EV_READ);

    connection->request.read_state = READ_END;

    if (connection->request.request != NULL)
        connection->request.request->body = NULL;

    connection->keep_alive = 0;

    istream_invoke_abort(&connection->request.body_reader.output);

    http_body_deinit(&connection->request.body_reader);
}

const struct istream http_server_request_stream = {
    .read = http_server_request_stream_read,
    .close = http_server_request_stream_close,
};
