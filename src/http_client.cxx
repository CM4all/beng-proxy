/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_client.hxx"
#include "http_response.hxx"
#include "fifo-buffer.h"
#include "strutil.h"
#include "header_parser.hxx"
#include "header_writer.hxx"
#include "pevent.h"
#include "http_body.hxx"
#include "istream-internal.h"
#include "istream_gb.hxx"
#include "async.h"
#include "growing_buffer.hxx"
#include "please.h"
#include "uri-verify.h"
#include "direct.h"
#include "fd-util.h"
#include "fd_util.h"
#include "stopwatch.h"
#include "strmap.h"
#include "completion.h"
#include "filtered_socket.hxx"
#include "util/Cast.hxx"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>
#include <socket/address.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct http_client {
    struct pool *pool, *caller_pool;

    const char *peer_name;

    struct stopwatch *stopwatch;

    /* I/O */
    struct filtered_socket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        /**
         * An "istream_optional" which blocks sending the request body
         * until the server has confirmed "100 Continue".
         */
        struct istream *body;

        struct istream *istream;
        char content_length_buffer[32];

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

    /* response */
    struct response {
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
         * This flag is true if we are currently calling the HTTP
         * response handler.  During this period,
         * http_client_response_stream_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

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

static const char *
get_peer_name(int fd)
{
     struct sockaddr_storage address;
     socklen_t address_length = sizeof(address);

     static char buffer[64];
     if (getpeername(fd, (struct sockaddr *)&address, &address_length) < 0 ||
         !socket_address_to_string(buffer, sizeof(buffer),
                                   (const struct sockaddr *)&address,
                                   address_length))
         return "unknown";

     return buffer;
}

static inline bool
http_client_valid(const struct http_client *client)
{
    return filtered_socket_valid(&client->socket);
}

static inline bool
http_client_check_direct(const struct http_client *client)
{
    assert(filtered_socket_fd_type(&client->socket) == ISTREAM_NONE ||
           filtered_socket_connected(&client->socket));
    assert(client->response.read_state == http_client::response::READ_BODY);

    return istream_check_direct(&client->response.body_reader.output,
                                filtered_socket_fd_type(&client->socket));
}

#if 0
static void
http_client_schedule_read(struct http_client *client)
{
    assert(client->input != nullptr);
    assert(!fifo_buffer_full(client->input));

    filtered_socket_schedule_read_timeout(&client->socket,
                                          client->request.istream != nullptr
                                          ? nullptr : &http_client_timeout);
}
#endif

static void
http_client_schedule_write(struct http_client *client)
{
    assert(filtered_socket_connected(&client->socket));

    filtered_socket_schedule_write(&client->socket);
}

/**
 * Release the socket held by this object.
 */
static void
http_client_release_socket(struct http_client *client, bool reuse)
{
    if (filtered_socket_has_filter(&client->socket))
        /* never reuse the socket if it was filtered */
        /* TODO: move the filtering layer to the tcp_stock to allow
           reusing connections */
        reuse = false;

    filtered_socket_abandon(&client->socket);
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
http_client_release(struct http_client *client, bool reuse)
{
    assert(client != nullptr);

    stopwatch_dump(client->stopwatch);

    if (filtered_socket_connected(&client->socket))
        http_client_release_socket(client, reuse);

    filtered_socket_destroy(&client->socket);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

static void
http_client_prefix_error(struct http_client *client, GError **error_r)
{
    g_prefix_error(error_r, "error on HTTP connection to '%s': ",
                   client->peer_name);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_headers(struct http_client *client, GError *error)
{
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS);

    if (filtered_socket_connected(&client->socket))
        http_client_release_socket(client, false);

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    http_client_prefix_error(client, &error);
    http_response_handler_invoke_abort(&client->request.handler, error);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_body(struct http_client *client, GError *error)
{
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(client->response.body != nullptr);

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    http_client_prefix_error(client, &error);
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
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS ||
           client->response.read_state == http_client::response::READ_BODY);

    if (client->response.read_state != http_client::response::READ_BODY)
        http_client_abort_response_headers(client, error);
    else
        http_client_abort_response_body(client, error);
}


/*
 * istream implementation for the response body
 *
 */

static inline struct http_client *
response_stream_to_http_client(struct istream *istream)
{
    return ContainerCast(istream, struct http_client,
                         response.body_reader.output);
}

static off_t
http_client_response_stream_available(struct istream *istream, bool partial)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != nullptr);
    assert(!client->socket.ended ||
           http_body_socket_is_done(&client->response.body_reader,
                                    &client->socket));
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    return http_body_available(&client->response.body_reader,
                               &client->socket, partial);
}

static void
http_client_response_stream_read(struct istream *istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != nullptr);
    assert(!client->socket.ended ||
           http_body_socket_is_done(&client->response.body_reader,
                                    &client->socket));
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(client->response.body_reader.output.handler != nullptr);
    assert(http_response_handler_used(&client->request.handler));

    if (client->response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    if (filtered_socket_connected(&client->socket))
        client->socket.base.direct = http_client_check_direct(client);

    filtered_socket_read(&client->socket,
                         http_body_require_more(&client->response.body_reader));
}

static int
http_client_response_stream_as_fd(struct istream *istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != nullptr);
    assert(!client->socket.ended ||
           http_body_socket_is_done(&client->response.body_reader,
                                    &client->socket));
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    if (!filtered_socket_connected(&client->socket) ||
        client->keep_alive ||
        /* must not be chunked */
        http_body_istream(&client->response.body_reader) != client->response.body)
        return -1;

    int fd = filtered_socket_as_fd(&client->socket);
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

    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));
    assert(!http_body_eof(&client->response.body_reader));

    stopwatch_event(client->stopwatch, "close");

    if (client->request.istream != nullptr)
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
    assert(client != nullptr);
    assert(filtered_socket_connected(&client->socket));

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
    assert(client != nullptr);

#ifdef __linux
    if (client->cork) {
        assert(filtered_socket_connected(&client->socket));
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
    assert(client != nullptr);
    assert(client->response.read_state == http_client::response::READ_STATUS);

    const char *space;
    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = (const char *)memchr(line + 6, ' ', length - 6)) == nullptr) {
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

    client->response.read_state = http_client::response::READ_HEADERS;
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
        (header_connection == nullptr && !client->response.http_1_0) ||
        (header_connection != nullptr &&
         strcasecmp(header_connection, "keep-alive") == 0);

    if (http_status_is_empty(client->response.status) ||
        client->response.no_body) {
        client->response.body = nullptr;
        client->response.read_state = http_client::response::READ_BODY;
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
    if (transfer_encoding == nullptr ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (unlikely(content_length_string == nullptr)) {
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
                client->response.body = nullptr;
                client->response.read_state = http_client::response::READ_BODY;
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

    client->response.read_state = http_client::response::READ_BODY;
    client->socket.base.direct = http_client_check_direct(client);
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_handle_line(struct http_client *client,
                        const char *line, size_t length)
{
    assert(client != nullptr);
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS);

    if (client->response.read_state == http_client::response::READ_STATUS)
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
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));

    stopwatch_event(client->stopwatch, "end");

    if (!filtered_socket_empty(&client->socket)) {
        daemon_log(2, "excess data after HTTP response\n");
        client->keep_alive = false;
    }

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    http_client_release(client, client->keep_alive &&
                        client->request.istream == nullptr);
}

static BufferedResult
http_client_parse_headers(struct http_client *client,
                          const void *_data, size_t length)
{
    assert(client != nullptr);
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS);
    assert(_data != nullptr);
    assert(length > 0);

    const char *const buffer = (const char *)_data;
    const char *buffer_end = buffer + length;

    /* parse line by line */
    const char *start = buffer, *end;
    while ((end = (const char *)memchr(start, '\n',
                                       buffer_end - start)) != nullptr) {
        const char *const next = end + 1;

        /* strip the line */
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        /* handle this line */
        if (!http_client_handle_line(client, start, end - start + 1))
            return BufferedResult::CLOSED;
        if (client->response.read_state != http_client::response::READ_HEADERS) {
            /* header parsing is finished */
            filtered_socket_consumed(&client->socket, next - buffer);
            return BufferedResult::AGAIN_EXPECT;
        }

        start = next;
    }

    /* remove the parsed part of the buffer */
    filtered_socket_consumed(&client->socket, start - buffer);
    return BufferedResult::MORE;
}

static void
http_client_response_stream_eof(struct http_client *client)
{
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_response_handler_used(&client->request.handler));
    assert(http_body_eof(&client->response.body_reader));

    /* this pointer must be cleared before forwarding the EOF event to
       our response body handler.  If we forget that, the handler
       might close the request body, leading to an assertion failure
       because http_client_request_stream_abort() calls
       http_client_abort_response_body(), not knowing that the
       response body is already finished  */
    client->response.body = nullptr;

    istream_deinit_eof(&client->response.body_reader.output);

    http_client_response_finished(client);
}

/**
 * Returns true if data has been consumed; false if nothing has been
 * consumed or if the client has been closed.
 */
static BufferedResult
http_client_feed_body(struct http_client *client,
                      const void *data, size_t length)
{
    assert(client != nullptr);
    assert(client->response.read_state == http_client::response::READ_BODY);

    size_t nbytes = http_body_feed_body(&client->response.body_reader,
                                        data, length);
    if (nbytes == 0)
        return filtered_socket_valid(&client->socket)
            ? BufferedResult::BLOCKING
            : BufferedResult::CLOSED;

    filtered_socket_consumed(&client->socket, nbytes);

    if (http_body_eof(&client->response.body_reader)) {
        http_client_response_stream_eof(client);
        return BufferedResult::CLOSED;
    }

    if (nbytes < length)
        return BufferedResult::PARTIAL;

    if (client->response.body_reader.rest > 0 ||
        /* the expect_more flag is true when the response body is
           chunked */
        client->socket.base.expect_more)
        return BufferedResult::MORE;

    return BufferedResult::OK;
}

static BufferedResult
http_client_feed_headers(struct http_client *client,
                         const void *data, size_t length)
{
    assert(client != nullptr);
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS);

    const BufferedResult result =
        http_client_parse_headers(client, data, length);
    if (result != BufferedResult::AGAIN_EXPECT)
        return result;

    /* the headers are finished, we can now report the response to
       the handler */
    assert(client->response.read_state == http_client::response::READ_BODY);

    if (client->response.status == HTTP_STATUS_CONTINUE) {
        assert(client->response.body == nullptr);

        if (client->request.body == nullptr) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "unexpected status 100");
#ifndef NDEBUG
            /* assertion workaround */
            client->response.read_state = http_client::response::READ_STATUS;
#endif
            http_client_abort_response_headers(client, error);
            return BufferedResult::CLOSED;
        }

        /* reset read_state, we're now expecting the real response */
        client->response.read_state = http_client::response::READ_STATUS;

        istream_optional_resume(client->request.body);
        client->request.body = nullptr;

        if (!filtered_socket_connected(&client->socket)) {
            GError *error = g_error_new_literal(http_client_quark(),
                                                HTTP_CLIENT_UNSPECIFIED,
                                                "Peer closed the socket prematurely after status 100");
#ifndef NDEBUG
            /* assertion workaround */
            client->response.read_state = http_client::response::READ_STATUS;
#endif
            http_client_abort_response_headers(client, error);
            return BufferedResult::CLOSED;
        }

        http_client_schedule_write(client);

        /* try again */
        return BufferedResult::AGAIN_EXPECT;
    } else if (client->request.body != nullptr) {
        /* the server begins sending a response - he's not interested
           in the request body, discard it now */
        istream_optional_discard(client->request.body);
        client->request.body = nullptr;
    }

    if ((client->response.body == nullptr ||
         http_body_socket_is_done(&client->response.body_reader,
                                 &client->socket)) &&
        filtered_socket_connected(&client->socket))
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        http_client_release_socket(client, client->keep_alive);

    pool_ref(client->pool);
    pool_ref(client->caller_pool);

    client->response.in_handler = true;
    http_response_handler_invoke_response(&client->request.handler,
                                          client->response.status,
                                          client->response.headers,
                                          client->response.body);
    client->response.in_handler = false;

    bool valid = http_client_valid(client);
    pool_unref(client->caller_pool);
    pool_unref(client->pool);

    if (!valid)
        return BufferedResult::CLOSED;

    if (client->response.body == nullptr) {
        http_client_response_finished(client);
        return BufferedResult::CLOSED;
    }

    /* now do the response body */
    return http_body_require_more(&client->response.body_reader)
        ? BufferedResult::AGAIN_EXPECT
        : BufferedResult::AGAIN_OPTIONAL;
}

static DirectResult
http_client_try_response_direct(struct http_client *client,
                                int fd, enum istream_direct fd_type)
{
    assert(filtered_socket_connected(&client->socket));
    assert(client->response.read_state == http_client::response::READ_BODY);
    assert(http_client_check_direct(client));

    ssize_t nbytes = http_body_try_direct(&client->response.body_reader,
                                          fd, fd_type);
    if (nbytes == ISTREAM_RESULT_BLOCKING)
        /* the destination fd blocks */
        return DirectResult::BLOCKING;

    if (nbytes == ISTREAM_RESULT_CLOSED)
        /* the stream (and the whole connection) has been closed
           during the direct() callback */
        return DirectResult::CLOSED;

    if (nbytes < 0) {
        if (errno == EAGAIN)
            /* the source fd (= ours) blocks */
            return DirectResult::EMPTY;

        return DirectResult::ERRNO;
    }

    if (nbytes == ISTREAM_RESULT_EOF) {
        if (client->request.istream != nullptr)
            istream_close_handler(client->request.istream);

        http_body_socket_eof(&client->response.body_reader, 0);
        http_client_release(client, false);
        return DirectResult::CLOSED;
   }

    if (http_body_eof(&client->response.body_reader)) {
        http_client_response_stream_eof(client);
        return DirectResult::CLOSED;
    }

    return DirectResult::OK;
}

static BufferedResult
http_client_feed(struct http_client *client, const void *data, size_t length)
{
    switch (client->response.read_state) {
    case http_client::response::READ_STATUS:
    case http_client::response::READ_HEADERS:
        return http_client_feed_headers(client, data, length);

    case http_client::response::READ_BODY:
        assert(client->response.body != nullptr);

        if (filtered_socket_connected(&client->socket) &&
            http_body_socket_is_done(&client->response.body_reader,
                                     &client->socket))
            /* we don't need the socket anymore, we've got everything
               we need in the input buffer */
            http_client_release_socket(client, client->keep_alive);

        return http_client_feed_body(client, data, length);
    }

    assert(false);
    gcc_unreachable();
}

/*
 * socket_wrapper handler
 *
 */

static BufferedResult
http_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    pool_ref(client->pool);
    BufferedResult result = http_client_feed(client, buffer, size);
    pool_unref(client->pool);

    return result;
}

static DirectResult
http_client_socket_direct(int fd, enum istream_direct fd_type, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    return http_client_try_response_direct(client, fd, fd_type);

}

static bool
http_client_socket_closed(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "end");

    if (client->request.istream != nullptr)
        istream_free(&client->request.istream);

    /* can't reuse the socket, it was closed by the peer */
    http_client_release_socket(client, false);

    return true;
}

static bool
http_client_socket_remaining(size_t remaining, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    if (client->response.read_state < http_client::response::READ_BODY)
        /* this information comes too early, we can't use it */
        return true;

    if (http_body_socket_eof(&client->response.body_reader, remaining)) {
        /* there's data left in the buffer: continue serving the
           buffer */
        return true;
    } else {
        /* finished: close the HTTP client */
        http_client_release(client, false);
        return false;
    }
}

static bool
http_client_socket_write(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    pool_ref(client->pool);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = filtered_socket_valid(&client->socket) &&
        filtered_socket_connected(&client->socket);
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            http_client_schedule_write(client);
        else
            filtered_socket_unschedule_write(&client->socket);
    }

    pool_unref(client->pool);
    return result;
}

static bool
http_client_socket_broken(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    /* the server has closed the connection, probably because he's not
       interested in our request body - that's ok; now we wait for his
       response */

    client->keep_alive = false;

    if (client->request.istream != nullptr)
        istream_free(&client->request.istream);

    filtered_socket_schedule_read_timeout(&client->socket, true,
                                          &http_client_timeout);

    return true;
}

static void
http_client_socket_error(GError *error, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "error");
    http_client_abort_response(client, error);
}

static constexpr BufferedSocketHandler http_client_socket_handler = {
    .data = http_client_socket_data,
    .direct = http_client_socket_direct,
    .closed = http_client_socket_closed,
    .remaining = http_client_socket_remaining,
    .write = http_client_socket_write,
    .broken = http_client_socket_broken,
    .error = http_client_socket_error,
};


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(filtered_socket_connected(&client->socket));

    client->request.got_data = true;

    ssize_t nbytes = filtered_socket_write(&client->socket, data, length);
    if (likely(nbytes >= 0)) {
        http_client_schedule_write(client);
        return (size_t)nbytes;
    }

    if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED ||
                   nbytes == WRITE_BROKEN))
        return 0;

    int _errno = errno;

    stopwatch_event(client->stopwatch, "error");

    GError *error = g_error_new(http_client_quark(), HTTP_CLIENT_IO,
                                "write error (%s)", strerror(_errno));
    http_client_abort_response(client, error);
    return 0;
}

static ssize_t
http_client_request_stream_direct(istream_direct type, int fd,
                                  size_t max_length, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(filtered_socket_connected(&client->socket));

    client->request.got_data = true;

    ssize_t nbytes = filtered_socket_write_from(&client->socket, fd, type,
                                                max_length);
    if (likely(nbytes > 0))
        http_client_schedule_write(client);
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED || nbytes == WRITE_BROKEN)
        return ISTREAM_RESULT_CLOSED;
    else if (likely(nbytes < 0)) {
        if (gcc_likely(errno == EAGAIN)) {
            client->request.got_data = false;
            filtered_socket_unschedule_write(&client->socket);
        }
    }

    return nbytes;
}

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    stopwatch_event(client->stopwatch, "request");

    assert(client->request.istream != nullptr);
    client->request.istream = nullptr;

    filtered_socket_unschedule_write(&client->socket);
    filtered_socket_read(&client->socket, false);
}

static void
http_client_request_stream_abort(GError *error, void *ctx)
{
    struct http_client *client = (struct http_client *)ctx;

    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS ||
           client->response.read_state == http_client::response::READ_BODY);

    stopwatch_event(client->stopwatch, "abort");

    client->request.istream = nullptr;

    if (client->response.read_state != http_client::response::READ_BODY)
        http_client_abort_response_headers(client, error);
    else if (client->response.body != nullptr)
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
    return ContainerCast(ao, struct http_client, request.async);
}

static void
http_client_request_abort(struct async_operation *ao)
{
    struct http_client *client
        = async_to_http_client(ao);

    stopwatch_event(client->stopwatch, "abort");

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == http_client::response::READ_STATUS ||
           client->response.read_state == http_client::response::READ_HEADERS);

    if (client->request.istream != nullptr)
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
                    const struct socket_filter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    const struct growing_buffer *headers,
                    struct istream *body, bool expect_100,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    assert(fd >= 0);
    assert(http_method_is_valid(method));
    assert(handler != nullptr);
    assert(handler->response != nullptr);

    if (!uri_path_verify_quick(uri)) {
        lease_direct_release(lease, lease_ctx, true);
        if (body != nullptr)
            istream_close_unused(body);

        GError *error = g_error_new(http_client_quark(),
                                    HTTP_CLIENT_UNSPECIFIED,
                                    "malformed request URI '%s'", uri);
        http_response_handler_direct_abort(handler, ctx, error);
        return;
    }

    struct pool *pool =
        pool_new_linear(caller_pool, "http_client_request", 8192);

    auto client = NewFromPool<struct http_client>(pool);
    client->stopwatch = stopwatch_fd_new(pool, fd, uri);
    client->pool = pool;
    client->peer_name = p_strdup(pool, get_peer_name(fd));

    filtered_socket_init(&client->socket, pool, fd, fd_type,
                         &http_client_timeout, &http_client_timeout,
                         filter, filter_ctx,
                         &http_client_socket_handler, client);
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "http_client_lease");

    client->response.read_state = http_client::response::READ_STATUS;
    client->response.no_body = http_method_is_empty(method);

    pool_ref(caller_pool);
    client->caller_pool = caller_pool;
    http_response_handler_set(&client->request.handler, handler, ctx);

    async_init(&client->request.async, &http_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    /* request line */

    const char *p = p_strcat(client->pool,
                             http_method_to_string(method), " ", uri,
                             " HTTP/1.1\r\n", nullptr);
    struct istream *request_line_stream = istream_string_new(client->pool, p);

    /* headers */

    struct istream *header_stream = headers != nullptr
        ? istream_gb_new(client->pool, headers)
        : istream_null_new(client->pool);

    struct growing_buffer *headers2 =
        growing_buffer_new(client->pool, 256);

    if (body != nullptr) {
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
            client->request.body = nullptr;
    } else
        client->request.body = nullptr;

    growing_buffer_write_buffer(headers2, "\r\n", 2);

    struct istream *header_stream2 = istream_gb_new(client->pool, headers2);

    /* request istream */

    client->request.istream = istream_cat_new(client->pool,
                                              request_line_stream,
                                              header_stream, header_stream2,
                                              body,
                                              nullptr);

    istream_handler_set(client->request.istream,
                        &http_client_request_stream_handler, client,
                        filtered_socket_direct_mask(&client->socket));

    filtered_socket_schedule_read_no_timeout(&client->socket, true);
    istream_read(client->request.istream);
}
