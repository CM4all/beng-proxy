/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_INTERNAL_H
#define __BENG_HTTP_SERVER_INTERNAL_H

#include "http_server.hxx"
#include "fifo_buffer.hxx"
#include "http_body.hxx"
#include "async.hxx"
#include "filtered_socket.hxx"

struct http_server_connection {
    struct pool *pool;

    /* I/O */
    FilteredSocket socket;

    /**
     * Track the total time for idle periods plus receiving all
     * headers from the client.  Unlike the #filtered_socket read
     * timeout, it is not refreshed after receiving some header data.
     */
    struct event idle_timeout;

    enum http_server_score score;

    /* handler */
    const struct http_server_connection_handler *handler;
    void *handler_ctx;

    /* info */

    const struct sockaddr *local_address;
    size_t local_address_length;

    const struct sockaddr *remote_address;
    size_t remote_address_length;

    const char *local_host_and_port;
    const char *remote_host_and_port, *remote_host;

    /* request */
    struct Request {
        enum {
            /** there is no request (yet); waiting for the request
                line */
            START,

            /** parsing request headers; waiting for empty line */
            HEADERS,

            /** reading the request body */
            BODY,

            /** the request has been consumed, and we are going to send the response */
            END
        } read_state;

        /**
         * This flag is true if we are currently calling the HTTP
         * request handler.  During this period,
         * http_server_request_stream_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /** has the client sent a HTTP/1.0 request? */
        bool http_1_0;

        /** did the client send an "Expect: 100-continue" header? */
        bool expect_100_continue;

        /** send a "417 Expectation Failed" response? */
        bool expect_failed;

        struct http_server_request *request;

        /** the request body reader; this variable is only valid if
            read_state==READ_BODY */
        HttpBodyReader body_reader;

        struct async_operation_ref async_ref;

        uint64_t bytes_received;
    } request;

    /** the response; this struct is only valid if
        read_state==READ_BODY||read_state==READ_END */
    struct {
        bool want_write;

        /**
         * Are we currently waiting for all output buffers to be
         * drained, before we can close the socket?
         *
         * @see BufferedSocketHandler::drained
         * @see http_server_socket_drained()
         */
        bool pending_drained;

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

    gcc_pure
    bool IsValid() const {
        return socket.IsValid() && socket.IsConnected();
    }

    /**
     * @return false if the connection has been closed
     */
    BufferedResult Feed(const void *data, size_t size);

    /**
     * Send data from the input buffer to the request body istream
     * handler.
     */
    BufferedResult FeedRequestBody(const void *data, size_t size);

    /**
     * Attempt a "direct" transfer of the request body.  Caller must
     * hold an additional pool reference.
     */
    DirectResult TryRequestBodyDirect(int fd, enum istream_direct fd_type);

    /**
     * @return false if the connection has been closed
     */
    bool MaybeSend100Continue();

    void ScheduleWrite() {
        response.want_write = true;
        socket.ScheduleWrite();
    }

    /**
     * @return false if the connection has been closed
     */
    bool TryWrite();

    /**
     * The last response on this connection is finished, and it should
     * be closed.
     */
    void Done();

    /**
     * The peer has closed the socket.
     */
    void Cancel();

    /**
     * A fatal error has occurred, and the connection should be closed
     * immediately, without sending any further information to the
     * client.  This invokes the error() handler method, but not
     * free().
     */
    void Error(GError *error);

    void Error(const char *msg);

    void ErrorErrno(const char *msg);
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

struct http_server_request *
http_server_request_new(struct http_server_connection *connection);

extern const struct istream_class http_server_request_stream;

extern const struct istream_handler http_server_response_stream_handler;

#endif
