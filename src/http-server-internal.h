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

    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *remote_host;

    /* request */
    struct {
        enum {
            /** there is no request (yet); waiting for the request
                line */
            READ_START,

            /** parsing request headers; waiting for empty line */
            READ_HEADERS,

            /** reading the request body */
            READ_BODY,

            /** the request has been consumed, and we are going to send the response */
            READ_END
        } read_state;

        /** has the client sent a HTTP/1.1 request? */
        bool http_1_1;

        /** did the client send an "Expect: 100-continue" header? */
        bool expect_100_continue;

        struct http_server_request *request;

        /** the request body reader; this variable is only valid if
            read_state==READ_BODY */
        struct http_body_reader body_reader;

        struct async_operation_ref async_ref;
    } request;

    /** the response; this struct is only valid if
        read_state==READ_BODY||read_state==READ_END */
    struct {
        bool writing_100_continue;
        http_status_t status;
        char status_buffer[64];
        char content_length_buffer[32];
        istream_t istream;
        off_t length;
    } response;

    /* connection settings */
    bool keep_alive;
#ifdef __linux
    bool cork;
#endif
};

static inline int
http_server_connection_valid(struct http_server_connection *connection)
{
    return connection->fd >= 0;
}

struct http_server_request *
http_server_request_new(struct http_server_connection *connection);

void
http_server_try_write(struct http_server_connection *connection);

void
http_server_maybe_send_100_continue(struct http_server_connection *connection);

void
http_server_consume_input(struct http_server_connection *connection);

void
http_server_try_read(struct http_server_connection *connection);

void
http_server_consume_body(struct http_server_connection *connection);

extern const struct istream http_server_request_stream;

extern const struct istream_handler http_server_response_stream_handler;

#endif
