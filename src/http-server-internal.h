/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_INTERNAL_H
#define __BENG_HTTP_SERVER_INTERNAL_H

#include "http-server.h"
#include "fifo-buffer.h"
#include "http-body.h"
#include "async.h"

#include <event.h>

struct http_server_connection {
    struct pool *pool;

    /* I/O */
    int fd;
    enum istream_direct fd_type;
    struct fifo_buffer *input;

    /**
     * The timeout for receiving data from the client.  Only active
     * while we're expecting data from the client.
     *
     * While receiving request headers, this timeout is not
     * reinitialized, and is a total timeout for receiving all of the
     * request headers.
     */
    struct event timeout;

    enum http_server_score score;

    /* handler */
    const struct http_server_connection_handler *handler;
    void *handler_ctx;

    /* info */

    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *local_host;
    const char *remote_host;

    /* request */
    struct {
        struct event event;

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

        bool want_read;

        /** has the client sent a HTTP/1.0 request? */
        bool http_1_0;

        /** did the client send an "Expect: 100-continue" header? */
        bool expect_100_continue;

        /** send a "417 Expectation Failed" response? */
        bool expect_failed;

        struct http_server_request *request;

        /** the request body reader; this variable is only valid if
            read_state==READ_BODY */
        struct http_body_reader body_reader;

        struct async_operation_ref async_ref;

        uint64_t bytes_received;
    } request;

    /** the response; this struct is only valid if
        read_state==READ_BODY||read_state==READ_END */
    struct {
        struct event event;

        bool want_write;

        bool writing_100_continue;
        http_status_t status;
        char status_buffer[64];
        char content_length_buffer[32];
        struct istream *istream;
        off_t length;

        uint64_t bytes_sent;
    } response;

    bool date_header;

    /* connection settings */
    bool keep_alive;
};

/**
 * The timeout of an idle connection (READ_START).
 */
extern const struct timeval http_server_idle_timeout;

/**
 * The total timeout of a client sending request headers.
 */
extern const struct timeval http_server_header_timeout;

/**
 * The timeout for reading more request data (READ_BODY).
 */
extern const struct timeval http_server_read_timeout;

/**
 * The timeout for writing more response data (READ_BODY, READ_END).
 */
extern const struct timeval http_server_write_timeout;

static inline int
http_server_connection_valid(struct http_server_connection *connection)
{
    return connection->fd >= 0;
}

static inline void
http_server_schedule_read(struct http_server_connection *connection)
{
    connection->request.want_read = true;

    const struct timeval *timeout =
        gcc_likely(connection->request.read_state == READ_START)
        ? &http_server_idle_timeout
        : (gcc_likely(connection->request.read_state == READ_BODY)
           ? &http_server_read_timeout
           : NULL);

    /* don't refresh timeout for READ_HEADERS */
    /* no timeout for READ_END, because this event is only there to
       detect a closing socket */

    if (timeout != NULL)
        evtimer_add(&connection->timeout, timeout);

    event_add(&connection->request.event, NULL);
}

static inline void
http_server_schedule_write(struct http_server_connection *connection)
{
    connection->response.want_write = true;
    event_add(&connection->response.event, &http_server_write_timeout);
}

/**
 * A fatal error has occurred, and the connection should be closed
 * immediately, without sending any further information to the client.
 * This invokes the error() handler method, but not free().
 */
void
http_server_error(struct http_server_connection *connection, GError *error);

void
http_server_error_message(struct http_server_connection *connection,
                          const char *msg);

void
http_server_errno(struct http_server_connection *connection, const char *msg);

struct http_server_request *
http_server_request_new(struct http_server_connection *connection);

/**
 * @return false if the connection has been closed
 */
bool
http_server_try_write(struct http_server_connection *connection);

/**
 * @return false if the connection has been closed
 */
bool
http_server_maybe_send_100_continue(struct http_server_connection *connection);

/**
 * @return true if something has been consumed; false if nothing has
 * been read or the connection has been closed (= do not continue)
 */
bool
http_server_consume_input(struct http_server_connection *connection);

/**
 * Read data into the input buffer.
 *
 * @return false if the connection has been closed
 */
bool
http_server_read_to_buffer(struct http_server_connection *connection);

/**
 * @return false if the connection has been closed
 */
bool
http_server_try_read(struct http_server_connection *connection);

/**
 * Send data from the input buffer to the request body istream
 * handler.
 *
 * @return true if something has been consumed (might also return true
 * when the input buffer is empty), false if nothing has been read or
 * the connection has been closed (= do not continue)
 */
bool
http_server_consume_body(struct http_server_connection *connection);

/**
 * The last response on this connection is finished, and it should be
 * closed.
 */
void
http_server_done(struct http_server_connection *connection);

/**
 * The peer has closed the socket.
 */
void
http_server_cancel(struct http_server_connection *connection);

extern const struct istream http_server_request_stream;

extern const struct istream_handler http_server_response_stream_handler;

#endif
