/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "http-response.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "header-parser.h"
#include "header-writer.h"
#include "pevent.h"
#include "http-body.h"
#include "istream-internal.h"
#include "istream-gb.h"
#include "async.h"
#include "growing-buffer.h"
#include "please.h"
#include "uri-verify.h"
#include "direct.h"
#include "fd-util.h"
#include "fd_util.h"
#include "stopwatch.h"
#include "strmap.h"
#include "completion.h"
#include "socket_wrapper.h"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct http_client {
    struct pool *pool, *caller_pool;

    struct stopwatch *stopwatch;

    /* I/O */
    struct socket_wrapper socket;
    struct lease_ref lease_ref;
    struct fifo_buffer *input;

    /* request */
    struct {
        /**
         * An "istream_optional" which blocks sending the request body
         * until the server has confirmed "100 Continue".
         */
        struct istream *body;

        struct istream *istream;
        char content_length_buffer[32];

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

    /* response */
    struct {
        enum {
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
        } read_state;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        bool no_body;

        /**
         * Has the server sent a HTTP/1.0 response?
         */
        bool http_1_0;

        http_status_t status;
        struct strmap *headers;
        struct istream *body;
        struct http_body_reader body_reader;
    } response;

    /* connection settings */
    bool keep_alive;
#ifdef __linux
    bool cork;
#endif
};

/**
 * With a request body of this size or larger, we send "Expect:
 * 100-continue".
 */
static const off_t EXPECT_100_THRESHOLD = 1024;

static const struct timeval http_client_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

static inline bool
http_client_valid(const struct http_client *client)
{
    return client->input != NULL;
}

static inline bool
http_client_check_direct(const struct http_client *client)
{
    assert(socket_wrapper_valid(&client->socket));
    assert(client->response.read_state == READ_BODY);

    return istream_check_direct(&client->response.body_reader.output,
                                client->socket.fd_type);
}

static bool
http_client_consume_body(struct http_client *client);

static void
http_client_try_read(struct http_client *client);

static void
http_client_schedule_read(struct http_client *client)
{
    assert(client->input != NULL);
    assert(!fifo_buffer_full(client->input));

    socket_wrapper_schedule_read_timeout(&client->socket,
                                         client->request.istream != NULL
                                         ? NULL : &http_client_timeout);
}

static void
http_client_schedule_write(struct http_client *client)
{
    assert(socket_wrapper_valid(&client->socket));

    socket_wrapper_schedule_write(&client->socket);
}

/**
 * Release the socket held by this object.
 */
static void
http_client_release_socket(struct http_client *client, bool reuse)
{
    assert(socket_wrapper_valid(&client->socket));

    socket_wrapper_abandon(&client->socket);
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
http_client_release(struct http_client *client, bool reuse)
{
    assert(client != NULL);

    stopwatch_dump(client->stopwatch);

    client->input = NULL;

    if (socket_wrapper_valid(&client->socket))
        http_client_release_socket(client, reuse);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_headers(struct http_client *client, GError *error)
{
    assert(socket_wrapper_valid(&client->socket));
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    if (socket_wrapper_valid(&client->socket))
        http_client_release_socket(client, false);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    http_response_handler_invoke_abort(&client->request.handler, error);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_body(struct http_client *client, GError *error)
{
    assert(client->response.read_state == READ_BODY);
    assert(client->response.body != NULL);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    istream_deinit_abort(&client->response.body_reader.output, error);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
static void
http_client_abort_response(struct http_client *client, GError *error)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_BODY);

    if (client->response.read_state != READ_BODY)
        http_client_abort_response_headers(client, error);
    else
        http_client_abort_response_body(client, error);
}


/*
 * istream implementation for the response body
 *
 */

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wextended-offsetof"
#endif

static inline struct http_client *
response_stream_to_http_client(struct istream *istream)
{
    return (struct http_client *)(((char*)istream) - offsetof(struct http_client, response.body_reader.output));
}

static off_t
http_client_response_stream_available(struct istream *istream, bool partial)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != NULL);
    assert(client->input != NULL);
    assert(socket_wrapper_valid(&client->socket) ||
           http_body_socket_is_done(&client->response.body_reader,
                                    client->input));
    assert(client->response.read_state == READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    return http_body_available(&client->response.body_reader,
                               client->input, partial);
}

static void
http_client_response_stream_read(struct istream *istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != NULL);
    assert(client->input != NULL);
    assert(socket_wrapper_valid(&client->socket) ||
           http_body_socket_is_done(&client->response.body_reader,
                                    client->input));
    assert(client->response.read_state == READ_BODY);
    assert(client->response.body_reader.output.handler != NULL);
    assert(http_response_handler_used(&client->request.handler));

    bool bret = http_client_consume_body(client);
    if (!bret)
        return;

    if (client->response.read_state == READ_BODY &&
        socket_wrapper_valid(&client->socket))
        http_client_try_read(client);
}

static int
http_client_response_stream_as_fd(struct istream *istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != NULL);
    assert(client->input != NULL);
    assert(socket_wrapper_valid(&client->socket) ||
           http_body_socket_is_done(&client->response.body_reader,
                                    client->input));
    assert(client->response.read_state == READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    if (!socket_wrapper_valid(&client->socket) || client->input == NULL ||
        !fifo_buffer_empty(client->input) || client->keep_alive ||
        /* must not be chunked */
        http_body_istream(&client->response.body_reader) != client->response.body)
        return -1;

    int fd = dup_cloexec(client->socket.fd);
    if (fd < 0)
        return -1;

    istream_deinit(&client->response.body_reader.output);
    http_client_release(client, false);
    return fd;
}

static void
http_client_response_stream_close(struct istream *istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client->response.read_state == READ_BODY);
    assert(http_response_handler_used(&client->request.handler));
    assert(!http_body_eof(&client->response.body_reader));

    stopwatch_event(client->stopwatch, "close");

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    istream_deinit(&client->response.body_reader.output);
    http_client_release(client, false);
}

static const struct istream_class http_client_response_stream = {
    .available = http_client_response_stream_available,
    .read = http_client_response_stream_read,
    .as_fd = http_client_response_stream_as_fd,
    .close = http_client_response_stream_close,
};


/*
static inline void
http_client_cork(struct http_client *client)
{
    assert(client != NULL);
    assert(socket_wrapper_valid(&client->socket));

#ifdef __linux
    if (!client->cork) {
        client->cork = true;
        socket_set_cork(client->fd, client->cork);
    }
#else
    (void)connection;
#endif
}

static inline void
http_client_uncork(struct http_client *client)
{
    assert(client != NULL);

#ifdef __linux
    if (client->cork) {
        assert(socket_wrapper_valid(&client->socket));
        client->cork = false;
        socket_set_cork(client->fd, client->cork);
    }
#else
    (void)connection;
#endif
}
*/

/**
 * @return false if the connection is closed
 */
static bool
http_client_parse_status_line(struct http_client *client,
                              const char *line, size_t length)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS);

    const char *space;
    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = memchr(line + 6, ' ', length - 6)) == NULL) {
        stopwatch_event(client->stopwatch, "malformed");

        GError *error =
            g_error_new_literal(http_client_quark(), HTTP_CLIENT_GARBAGE,
                                "malformed HTTP status line");
        http_client_abort_response_headers(client, error);
        return false;
    }

    client->response.http_1_0 = line[7] == '0' &&
        line[6] == '.' && line[5] == '1';

    length = line + length - space - 1;
    line = space + 1;

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
        stopwatch_event(client->stopwatch, "malformed");

        GError *error =
            g_error_new_literal(http_client_quark(), HTTP_CLIENT_GARBAGE,
                                "no HTTP status found");
        http_client_abort_response_headers(client, error);
        return false;
    }

    client->response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (unlikely(!http_status_is_valid(client->response.status))) {
        stopwatch_event(client->stopwatch, "malformed");

        GError *error =
            g_error_new(http_client_quark(), HTTP_CLIENT_GARBAGE,
                        "invalid HTTP status %d",
                        client->response.status);
        http_client_abort_response_headers(client, error);
        return false;
    }

    client->response.read_state = READ_HEADERS;
    client->response.headers = strmap_new(client->caller_pool, 64);
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_headers_finished(struct http_client *client)
{
    stopwatch_event(client->stopwatch, "headers");

    const char *header_connection =
        strmap_remove(client->response.headers, "connection");
    client->keep_alive =
        (header_connection == NULL && !client->response.http_1_0) ||
        (header_connection != NULL &&
         strcasecmp(header_connection, "keep-alive") == 0);

    if (http_status_is_empty(client->response.status) ||
        client->response.no_body) {
        client->response.body = NULL;
        client->response.read_state = READ_BODY;
        return true;
    }

    const char *transfer_encoding = strmap_remove(client->response.headers,
                                                  "transfer-encoding");
    const char *content_length_string = strmap_remove(client->response.headers,
                                                      "content-length");

    /* remove the other hop-by-hop response headers */
    strmap_remove(client->response.headers, "proxy-authenticate");
    strmap_remove(client->response.headers, "upgrade");

    off_t content_length;
    bool chunked;
    if (transfer_encoding == NULL ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (unlikely(content_length_string == NULL)) {
            if (client->keep_alive) {
                stopwatch_event(client->stopwatch, "malformed");

                GError *error =
                    g_error_new_literal(http_client_quark(),
                                        HTTP_CLIENT_UNSPECIFIED,
                                        "no Content-Length header response");
                http_client_abort_response_headers(client, error);
                return false;
            }
            content_length = (off_t)-1;
        } else {
            char *endptr;
            content_length = (off_t)strtoull(content_length_string,
                                             &endptr, 10);
            if (unlikely(endptr == content_length_string || *endptr != 0 ||
                         content_length < 0)) {
                stopwatch_event(client->stopwatch, "malformed");

                GError *error =
                    g_error_new_literal(http_client_quark(),
                                        HTTP_CLIENT_UNSPECIFIED,
                                        "invalid Content-Length header in response");
                http_client_abort_response_headers(client, error);
                return false;
            }

            if (content_length == 0) {
                client->response.body = NULL;
                client->response.read_state = READ_BODY;
                return true;
            }
        }

        chunked = false;
    } else {
        /* chunked */

        content_length = (off_t)-1;
        chunked = true;
    }

    client->response.body
        = http_body_init(&client->response.body_reader,
                         &http_client_response_stream,
                         client->pool,
                         client->pool,
                         content_length,
                         chunked);

    client->response.read_state = READ_BODY;
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_handle_line(struct http_client *client,
                        const char *line, size_t length)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    if (client->response.read_state == READ_STATUS)
        return http_client_parse_status_line(client, line, length);
    else if (length > 0) {
        header_parse_line(client->pool,
                          client->response.headers,
                          line, length);
        return true;
    } else
        return http_client_headers_finished(client);
}

static void
http_client_response_finished(struct http_client *client)
{
    assert(client->response.read_state == READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    stopwatch_event(client->stopwatch, "end");

    if (client->input != NULL && !fifo_buffer_empty(client->input)) {
        daemon_log(2, "excess data after HTTP response\n");
        client->keep_alive = false;
    }

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    http_client_release(client, client->keep_alive &&
                        client->request.istream == NULL);
}

static enum completion
http_client_parse_headers(struct http_client *client)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    size_t length;
    const char *buffer = fifo_buffer_read(client->input, &length);
    if (buffer == NULL)
        return C_MORE;

    assert(length > 0);
    const char *buffer_end = buffer + length;

    /* parse line by line */
    const char *start = buffer, *end;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        const char *const next = end + 1;

        /* strip the line */
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        /* handle this line */
        if (!http_client_handle_line(client, start, end - start + 1))
            return C_CLOSED;

        if (client->response.read_state != READ_HEADERS) {
            /* header parsing is finished */
            fifo_buffer_consume(client->input, next - buffer);
            return C_DONE;
        }

        start = next;
    }

    /* remove the parsed part of the buffer */
    fifo_buffer_consume(client->input, start - buffer);

    if (fifo_buffer_full(client->input)) {
        /* the line is too large for our input buffer */
        GError *error =
            g_error_new_literal(http_client_quark(),
                                HTTP_CLIENT_UNSPECIFIED,
                                "response header too long");
        http_client_abort_response_headers(client, error);
        return C_CLOSED;
    }

    http_client_schedule_read(client);

    return C_MORE;
}

static void
http_client_response_stream_eof(struct http_client *client)
{
    assert(client->response.read_state == READ_BODY);
    assert(http_response_handler_used(&client->request.handler));
    assert(http_body_eof(&client->response.body_reader));

    /* this pointer must be cleared before forwarding the EOF event to
       our response body handler.  If we forget that, the handler
       might close the request body, leading to an assertion failure
       because http_client_request_stream_abort() calls
       http_client_abort_response_body(), not knowing that the
       response body is already finished  */
    client->response.body = NULL;

    istream_deinit_eof(&client->response.body_reader.output);

    http_client_response_finished(client);
}

/**
 * Returns true if data has been consumed; false if nothing has been
 * consumed or if the client has been closed.
 */
static bool
http_client_consume_body(struct http_client *client)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_BODY);

    if (fifo_buffer_full(client->input))
        /* remove the "READ" event - if the buffer is full, and
           http_body_consume_body() blocks, I don't want to check if
           the connection has been closed, so we're just removing this
           event now; it will be added again at the end of this
           function */
        socket_wrapper_unschedule_read(&client->socket);

    size_t nbytes = http_body_consume_body(&client->response.body_reader,
                                           client->input);
    if (nbytes == 0)
        return false;

    if (http_body_eof(&client->response.body_reader)) {
        http_client_response_stream_eof(client);
        return false;
    }

    if (socket_wrapper_valid(&client->socket))
        http_client_schedule_read(client);
    return true;
}

static enum completion
http_client_consume_headers(struct http_client *client)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    const enum completion c = http_client_parse_headers(client);
    if (c != C_DONE)
        return c;

    /* the headers are finished, we can now report the response to
       the handler */
    assert(client->response.read_state == READ_BODY);

    if (client->response.status == HTTP_STATUS_CONTINUE) {
        assert(client->response.body == NULL);

        if (client->request.body == NULL) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "unexpected status 100");
#ifndef NDEBUG
            /* assertion workaround */
            client->response.read_state = READ_STATUS;
#endif
            http_client_abort_response_headers(client, error);
            return C_CLOSED;
        }

        /* reset read_state, we're now expecting the real response */
        client->response.read_state = READ_STATUS;

        istream_optional_resume(client->request.body);
        client->request.body = NULL;

        http_client_schedule_read(client);
        http_client_schedule_write(client);

        /* try again */
        return http_client_consume_headers(client);
    } else if (client->request.body != NULL) {
        /* the server begins sending a response - he's not interested
           in the request body, discard it now */
        istream_optional_discard(client->request.body);
        client->request.body = NULL;
    }

    if (client->response.body == NULL ||
        http_body_socket_is_done(&client->response.body_reader, client->input))
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        http_client_release_socket(client, client->keep_alive);

    pool_ref(client->pool);
    pool_ref(client->caller_pool);

    http_response_handler_invoke_response(&client->request.handler,
                                          client->response.status,
                                          client->response.headers,
                                          client->response.body);

    bool valid = http_client_valid(client);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);

    if (!valid)
        return C_CLOSED;

    if (client->response.body == NULL) {
        http_client_response_finished(client);
        return C_CLOSED;
    }

    return C_DONE;
}

/**
 * @return false if the HTTP client has been closed
 */
static bool
http_client_try_response_direct(struct http_client *client)
{
    assert(socket_wrapper_valid(&client->socket));
    assert(client->response.read_state == READ_BODY);
    assert(http_client_check_direct(client));

    ssize_t nbytes = http_body_try_direct(&client->response.body_reader,
                                          client->socket.fd,
                                          client->socket.fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return true;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback */
        return false;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            /* the source fd (= ours) blocks */
            http_client_schedule_read(client);
            return true;
        }

        GError *error = g_error_new(http_client_quark(), errno,
                                    "read error: %s", strerror(errno));

        stopwatch_event(client->stopwatch, "error");

        http_client_abort_response_body(client, error);
        return false;
    }

    if (nbytes == ISTREAM_RESULT_EOF)
        return true;

    if (http_body_eof(&client->response.body_reader)) {
        http_client_response_stream_eof(client);
        return false;
    }

    http_client_schedule_read(client);
    return true;
}

static void
http_client_try_read_buffered(struct http_client *client)
{
    ssize_t nbytes = socket_wrapper_read_to_buffer(&client->socket,
                                                   client->input,
                                                   INT_MAX);
    assert(nbytes != -2);

    if (nbytes == 0) {
        if (client->response.read_state == READ_BODY) {
            stopwatch_event(client->stopwatch, "end");

            if (client->request.istream != NULL)
                istream_close_handler(client->request.istream);

            if (http_body_socket_eof(&client->response.body_reader,
                                     client->input))
                /* there's data left in the buffer: only release the
                   socket, continue serving the buffer */
                http_client_release_socket(client, false);
            else
                /* finished: close the HTTP client */
                http_client_release(client, false);
        } else if (client->response.read_state == READ_STATUS &&
                   fifo_buffer_empty(client->input)) {
            stopwatch_event(client->stopwatch, "error");

            GError *error =
                g_error_new_literal(http_client_quark(), HTTP_CLIENT_REFUSED,
                                    "server refused request");
            http_client_abort_response_headers(client, error);
        } else {
            stopwatch_event(client->stopwatch, "error");

            GError *error =
                g_error_new_literal(http_client_quark(),
                                    HTTP_CLIENT_PREMATURE,
                                    "server closed connection "
                                    "during response headers");
            http_client_abort_response_headers(client, error);
        }

        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            http_client_schedule_read(client);
            return;
        }

        stopwatch_event(client->stopwatch, "error");

        GError *error = g_error_new(http_client_quark(),
                                    HTTP_CLIENT_IO,
                                    "read error (%s)", strerror(errno));
        http_client_abort_response(client, error);
        return;
    }

    if (client->response.read_state == READ_BODY ||
        http_client_consume_headers(client) == C_DONE) {
        assert(client->response.body != NULL);

        if (socket_wrapper_valid(&client->socket) &&
            http_body_socket_is_done(&client->response.body_reader, client->input))
            /* we don't need the socket anymore, we've got everything we
               need in the input buffer */
            http_client_release_socket(client, client->keep_alive);

        http_client_consume_body(client);
    }
}

static void
http_client_try_read(struct http_client *client)
{
    assert(socket_wrapper_valid(&client->socket));

    if (client->response.read_state == READ_BODY &&
        http_client_check_direct(client)) {
        if (!fifo_buffer_empty(client->input)) {
            /* there is still data in the body, which we have to
               consume before we do direct splice() */
            if (!http_client_consume_body(client) ||
                !fifo_buffer_empty(client->input))
                return;

            /* at this point, the handler might have changed, and the
               new handler might not support "direct" transfer - check
               again */
            if (!http_client_check_direct(client)) {
                http_client_schedule_read(client);
                return;
            }
        }

        http_client_try_response_direct(client);
    } else
        http_client_try_read_buffered(client);
}

/*
 * socket_wrapper handler
 *
 */

static bool
http_client_socket_write(void *ctx)
{
    struct http_client *client = ctx;

    pool_ref(client->pool);

    istream_read(client->request.istream);

    bool result = socket_wrapper_valid(&client->socket);
    pool_unref(client->pool);
    return result;
}

static bool
http_client_socket_read(void *ctx)
{
    struct http_client *client = ctx;

    pool_ref(client->pool);

    http_client_try_read(client);

    bool result = socket_wrapper_valid(&client->socket);
    pool_unref(client->pool);
    return result;
}

static bool
http_client_socket_timeout(void *ctx)
{
    struct http_client *client = ctx;

    stopwatch_event(client->stopwatch, "timeout");

    GError *error = g_error_new_literal(http_client_quark(),
                                        HTTP_CLIENT_TIMEOUT,
                                        "receive timeout");
    http_client_abort_response(client, error);
    return false;
}

static const struct socket_handler http_client_socket_handler = {
    .read = http_client_socket_read,
    .write = http_client_socket_write,
    .timeout = http_client_socket_timeout,
};


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_client *client = ctx;

    assert(socket_wrapper_valid(&client->socket));

    ssize_t nbytes = socket_wrapper_write(&client->socket, data, length);
    if (likely(nbytes >= 0)) {
        http_client_schedule_write(client);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        http_client_schedule_write(client);
        return 0;
    }

    int _errno = errno;

    if (errno == EPIPE || errno == ECONNRESET) {
        /* the server has closed the connection, probably because he's
           not interested in our request body - if he has already sent
           the response, everything's fine */
        bool valid;

        pool_ref(client->pool);
        /* see if we can receive the full response now */
        http_client_try_read(client);
        valid = http_client_valid(client);
        pool_unref(client->pool);

        if (!valid)
            /* this client is done (either response finished or an
               error occured) - return */
            return 0;

        /* at this point, the response is not finished, and we bail
           out by aborting the HTTP client */
    }

    stopwatch_event(client->stopwatch, "error");

    GError *error = g_error_new(http_client_quark(), HTTP_CLIENT_IO,
                                "write error (%s)", strerror(_errno));
    http_client_abort_response(client, error);
    return 0;
}

static ssize_t
http_client_request_stream_direct(istream_direct_t type, int fd,
                                  size_t max_length, void *ctx)
{
    struct http_client *client = ctx;

    assert(socket_wrapper_valid(&client->socket));

    ssize_t nbytes = socket_wrapper_write_from(&client->socket, fd, type,
                                               max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!socket_wrapper_ready_for_writing(&client->socket)) {
            http_client_schedule_write(client);
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = socket_wrapper_write_from(&client->socket, fd, type,
                                           max_length);
    }

    if (likely(nbytes > 0))
        http_client_schedule_write(client);

    return nbytes;
}

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client *client = ctx;

    stopwatch_event(client->stopwatch, "request");

    client->request.istream = NULL;

    socket_wrapper_unschedule_write(&client->socket);

    if (!fifo_buffer_full(client->input))
        http_client_schedule_read(client);
}

static void
http_client_request_stream_abort(GError *error, void *ctx)
{
    struct http_client *client = ctx;

    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_BODY);

    stopwatch_event(client->stopwatch, "abort");

    client->request.istream = NULL;

    if (client->response.read_state != READ_BODY)
        http_client_abort_response_headers(client, error);
    else if (client->response.body != NULL)
        http_client_abort_response_body(client, error);
    else
        g_error_free(error);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
    .direct = http_client_request_stream_direct,
    .eof = http_client_request_stream_eof,
    .abort = http_client_request_stream_abort,
};


/*
 * async operation
 *
 */

static struct http_client *
async_to_http_client(struct async_operation *ao)
{
    return (struct http_client*)(((char*)ao) - offsetof(struct http_client, request.async));
}

static void
http_client_request_abort(struct async_operation *ao)
{
    struct http_client *client
        = async_to_http_client(ao);

    stopwatch_event(client->stopwatch, "abort");

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    http_client_release(client, false);
}

static const struct async_operation_class http_client_async_operation = {
    .abort = http_client_request_abort,
};


/*
 * constructor
 *
 */

void
http_client_request(struct pool *caller_pool,
                    int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    const struct growing_buffer *headers,
                    struct istream *body, bool expect_100,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    assert(fd >= 0);
    assert(http_method_is_valid(method));
    assert(handler != NULL);
    assert(handler->response != NULL);

    if (!uri_path_verify_quick(uri)) {
        lease_direct_release(lease, lease_ctx, true);
        if (body != NULL)
            istream_close_unused(body);

        GError *error = g_error_new(http_client_quark(),
                                    HTTP_CLIENT_UNSPECIFIED,
                                    "malformed request URI '%s'", uri);
        http_response_handler_direct_abort(handler, ctx, error);
        return;
    }

    struct pool *pool =
        pool_new_linear(caller_pool, "http_client_request", 8192);

    struct http_client *client = p_malloc(pool, sizeof(*client));
    client->stopwatch = stopwatch_fd_new(pool, fd, uri);
    client->pool = pool;

    socket_wrapper_init(&client->socket, pool, fd, fd_type,
                        NULL, &http_client_timeout,
                        &http_client_socket_handler, client);
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "http_client_lease");

    client->response.read_state = READ_STATUS;
    client->response.no_body = http_method_is_empty(method);

    client->input = fifo_buffer_new(client->pool, 4096);

    pool_ref(caller_pool);
    client->caller_pool = caller_pool;
    http_response_handler_set(&client->request.handler, handler, ctx);

    async_init(&client->request.async, &http_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    /* request line */

    const char *p = p_strcat(client->pool,
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", NULL);
    struct istream *request_line_stream = istream_string_new(client->pool, p);

    /* headers */

    struct istream *header_stream = headers != NULL
        ? istream_gb_new(client->pool, headers)
        : istream_null_new(client->pool);

    struct growing_buffer *headers2 =
        growing_buffer_new(client->pool, 256);

    if (body != NULL) {
        off_t content_length = istream_available(body, false);
        if (content_length == (off_t)-1) {
            header_write(headers2, "transfer-encoding", "chunked");
            body = istream_chunked_new(client->pool, body);
        } else {
            snprintf(client->request.content_length_buffer,
                     sizeof(client->request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers2, "content-length",
                         client->request.content_length_buffer);
        }

        off_t available = expect_100 ? istream_available(body, true) : 0;
        if (available < 0 || available >= EXPECT_100_THRESHOLD) {
            /* large request body: ask the server for confirmation
               that he's really interested */
            header_write(headers2, "expect", "100-continue");
            body = client->request.body = istream_optional_new(pool, body);
        } else
            /* short request body: send it immediately */
            client->request.body = NULL;
    } else
        client->request.body = NULL;

    growing_buffer_write_buffer(headers2, "\r\n", 2);

    struct istream *header_stream2 = istream_gb_new(client->pool, headers2);

    /* request istream */

    client->request.istream = istream_cat_new(client->pool,
                                              request_line_stream,
                                              header_stream, header_stream2,
                                              body,
                                              NULL);

    istream_handler_set(client->request.istream,
                        &http_client_request_stream_handler, client,
                        istream_direct_mask_to(fd_type));

    http_client_schedule_read(client);

    istream_read(client->request.istream);
}
