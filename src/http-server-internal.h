/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_INTERNAL_H
#define __BENG_HTTP_SERVER_INTERNAL_H

#include "http-server.h"
#include "fifo-buffer.h"
#include "event2.h"
#include "http-body.h"
#include "async.h"

struct http_server_connection {
    pool_t pool;

    /* I/O */
    int fd;
    struct event2 event;
    fifo_buffer_t input;

    /* handler */
    const struct http_server_connection_handler *handler;
    void *handler_ctx;

    /* info */

    const char *remote_host;

    /* request */
    struct {
        enum {
            READ_START,
            READ_HEADERS,
            READ_BODY,
            READ_END
        } read_state;

        /** did the client send an "Expect: 100-continue" header? */
        int expect_100_continue;

        struct http_server_request *request;

        struct http_body_reader body_reader;

        struct async_operation_ref async_ref;
    } request;

    /* response */
    struct {
        bool writing_100_continue;
        char status_buffer[64];
        char content_length_buffer[32];
        istream_t istream;
    } response;

    /* connection settings */
    bool keep_alive;
#ifdef __linux
    bool cork;
#endif
};

static inline int
http_server_connection_valid(http_server_connection_t connection)
{
    return connection->fd >= 0;
}

struct http_server_request *
http_server_request_new(http_server_connection_t connection);

void
http_server_maybe_send_100_continue(http_server_connection_t connection);

void
http_server_try_read(http_server_connection_t connection);

void
http_server_request_free(struct http_server_request **request_r);

void
http_server_consume_body(http_server_connection_t connection);

extern const struct istream http_server_request_stream;

extern const struct istream_handler http_server_response_stream_handler;

#endif
