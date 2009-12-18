/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-client.h"
#include "http-response.h"
#include "fifo-buffer.h"
#include "strutil.h"
#include "buffered-io.h"
#include "header-parser.h"
#include "header-writer.h"
#include "event2.h"
#include "http-body.h"
#include "istream-internal.h"
#include "async.h"
#include "growing-buffer.h"
#include "lease.h"
#include "uri-verify.h"
#include "direct.h"
#include "fd-util.h"
#include "stopwatch.h"

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>

struct http_client {
    pool_t pool, caller_pool;

    struct stopwatch *stopwatch;

    /* I/O */
    int fd;
    enum istream_direct fd_type;
    struct lease_ref lease_ref;
    struct event2 event;
    struct fifo_buffer *input;

    /* request */
    struct {
        istream_t istream;
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
        istream_t body;
        struct http_body_reader body_reader;
    } response;

    /* connection settings */
    bool keep_alive;
#ifdef __linux
    bool cork;
#endif
};

static inline bool
http_client_valid(struct http_client *client)
{
    return client->input != NULL;
}

static bool
http_client_consume_body(struct http_client *client);

static void
http_client_try_read(struct http_client *client);

/**
 * Release the socket held by this object.
 */
static void
http_client_release_socket(struct http_client *client, bool reuse)
{
    assert(client->fd >= 0);

    event2_set(&client->event, 0);
    event2_commit(&client->event);
    client->fd = -1;
    lease_release(&client->lease_ref, reuse);
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

    if (client->fd >= 0)
        http_client_release_socket(client, reuse);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_headers(struct http_client *client)
{
    assert(client->fd >= 0);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    http_response_handler_invoke_abort(&client->request.handler);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_body(struct http_client *client)
{
    assert(client->response.read_state == READ_BODY);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    istream_deinit_abort(&client->response.body_reader.output);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers/body from the HTTP
 * server.
 */
static void
http_client_abort_response(struct http_client *client)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_BODY);

    if (client->response.read_state != READ_BODY)
        http_client_abort_response_headers(client);
    else
        http_client_abort_response_body(client);
}


/*
 * istream implementation for the response body
 *
 */

static inline struct http_client *
response_stream_to_http_client(istream_t istream)
{
    return (struct http_client *)(((char*)istream) - offsetof(struct http_client, response.body_reader.output));
}

static off_t
http_client_response_stream_available(istream_t istream, bool partial)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != NULL);
    assert(client->input != NULL);
    assert(client->fd >= 0 ||
           http_body_socket_is_done(&client->response.body_reader,
                                    client->input));
    assert(client->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&client->request.handler));

    return http_body_available(&client->response.body_reader,
                               client->input, partial);
}

static void
http_client_response_stream_read(istream_t istream)
{
    struct http_client *client = response_stream_to_http_client(istream);
    bool bret;

    assert(client != NULL);
    assert(client->input != NULL);
    assert(client->fd >= 0 ||
           http_body_socket_is_done(&client->response.body_reader,
                                    client->input));
    assert(client->response.read_state == READ_BODY);
    assert(client->response.body_reader.output.handler != NULL);
    assert(!http_response_handler_defined(&client->request.handler));

    bret = http_client_consume_body(client);
    if (!bret)
        return;

    if (client->response.read_state == READ_BODY && client->fd >= 0)
        http_client_try_read(client);
}

static void
http_client_response_stream_close(istream_t istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&client->request.handler));
    assert(!http_body_eof(&client->response.body_reader));

    stopwatch_event(client->stopwatch, "close");
    http_client_abort_response_body(client);
}

static const struct istream http_client_response_stream = {
    .available = http_client_response_stream_available,
    .read = http_client_response_stream_read,
    .close = http_client_response_stream_close,
};


/*
static inline void
http_client_cork(struct http_client *client)
{
    assert(client != NULL);
    assert(client->fd >= 0);

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
        assert(client->fd >= 0);
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
    const char *space;

    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS);

    if (length < 10 || memcmp(line, "HTTP/", 5) != 0 ||
        (space = memchr(line + 6, ' ', length - 6)) == NULL) {
        daemon_log(2, "http_client: malformed HTTP status line\n");
        stopwatch_event(client->stopwatch, "malformed");
        http_client_abort_response_headers(client);
        return false;
    }

    client->response.http_1_0 = line[7] == '0' &&
        line[6] == '.' && line[5] == '1';

    length = line + length - space - 1;
    line = space + 1;

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
        daemon_log(2, "http_client: no HTTP status found\n");
        stopwatch_event(client->stopwatch, "malformed");
        http_client_abort_response_headers(client);
        return false;
    }

    client->response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (unlikely(!http_status_is_valid(client->response.status))) {
        daemon_log(2, "http_client: invalid HTTP status %d\n",
                   client->response.status);
        stopwatch_event(client->stopwatch, "malformed");
        http_client_abort_response_headers(client);
        return false;
    }

    client->response.read_state = READ_HEADERS;
    client->response.headers = strmap_new(client->pool, 64);
    return true;
}

/**
 * @return false if the connection is closed
 */
static bool
http_client_headers_finished(struct http_client *client)
{
    const char *header_connection, *transfer_encoding, *content_length_string;
    char *endptr;
    off_t content_length;
    bool chunked;

    stopwatch_event(client->stopwatch, "headers");

    header_connection = strmap_remove(client->response.headers, "connection");
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

    transfer_encoding = strmap_remove(client->response.headers,
                                      "transfer-encoding");
    content_length_string = strmap_remove(client->response.headers,
                                          "content-length");

    /* remove the other hop-by-hop response headers */
    strmap_remove(client->response.headers, "proxy-authenticate");
    strmap_remove(client->response.headers, "upgrade");

    if (transfer_encoding == NULL ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (unlikely(content_length_string == NULL)) {
            if (client->keep_alive) {
                daemon_log(2, "http_client: no Content-Length header response\n");
                stopwatch_event(client->stopwatch, "malformed");
                http_client_abort_response_headers(client);
                return false;
            }
            content_length = (off_t)-1;
        } else {
            content_length = strtoul(content_length_string, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                daemon_log(2, "http_client: invalid Content-Length header in response\n");
                stopwatch_event(client->stopwatch, "malformed");
                http_client_abort_response_headers(client);
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
    assert(!http_response_handler_defined(&client->request.handler));

    stopwatch_event(client->stopwatch, "end");

    if (!fifo_buffer_empty(client->input)) {
        daemon_log(2, "excess data after HTTP response\n");
        client->keep_alive = false;
    }

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    http_client_release(client, client->keep_alive &&
                        client->request.istream == NULL);
}

/**
 * @return false if nothing has been parsed
 */
static bool
http_client_parse_headers(struct http_client *client)
{
    const char *buffer, *buffer_end, *start, *end, *next = NULL;
    size_t length;
    bool bret;

    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    buffer = fifo_buffer_read(client->input, &length);
    if (buffer == NULL)
        return false;

    assert(length > 0);
    buffer_end = buffer + length;

    /* parse line by line */
    start = buffer;
    while ((end = memchr(start, '\n', buffer_end - start)) != NULL) {
        next = end + 1;

        /* strip the line */
        --end;
        while (end >= start && char_is_whitespace(*end))
            --end;

        /* handle this line */
        bret = http_client_handle_line(client, start, end - start + 1);
        if (!bret)
            return false;

        if (client->response.read_state != READ_HEADERS)
            /* header parsing is finished */
            break;

        start = next;
    }

    if (end == NULL)
        /* not enough data to finish this line, let libevent handle
           this */
        event2_or(&client->event, EV_READ);

    if (next == NULL)
        /* not a single line was processed - skip the following
           checks */
        return false;

    /* remove the parsed part of the buffer */
    fifo_buffer_consume(client->input, next - buffer);

    return true;
}

static void
http_client_response_stream_eof(struct http_client *client)
{
    assert(client->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&client->request.handler));
    assert(http_body_eof(&client->response.body_reader));

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
    size_t nbytes;

    assert(client != NULL);
    assert(client->response.read_state == READ_BODY);

    if (fifo_buffer_full(client->input))
        /* remove the "READ" event - if the buffer is full, and
           http_body_consume_body() blocks, I don't want to check if
           the connection has been closed, so we're just removing this
           event now; it will be added again at the end of this
           function */
        event2_nand(&client->event, EV_READ);

    nbytes = http_body_consume_body(&client->response.body_reader, client->input);
    if (nbytes == 0)
        return false;

    if (http_body_eof(&client->response.body_reader)) {
        http_client_response_stream_eof(client);
        return false;
    }

    event2_or(&client->event, EV_READ);
    return true;
}

/**
 * Returns false if the client has been closed or if the headers are
 * incomplete.
 */
static bool
http_client_consume_headers(struct http_client *client)
{
    bool bret;

    assert(client != NULL);
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    do {
        bret = http_client_parse_headers(client);
        if (!bret)
            return false;
    } while (client->response.read_state == READ_HEADERS);

    /* the headers are finished, we can now report the response to
       the handler */
    assert(client->response.read_state == READ_BODY);

    if (client->response.body == NULL ||
        http_body_socket_is_done(&client->response.body_reader, client->input))
        /* we don't need the socket anymore, we've got everything we
           need in the input buffer */
        http_client_release_socket(client, client->keep_alive);

    http_response_handler_invoke_response(&client->request.handler,
                                          client->response.status,
                                          client->response.headers,
                                          client->response.body);

    if (!http_client_valid(client))
        return false;

    if (client->response.body == NULL) {
        http_client_response_finished(client);
        return false;
    }

    return true;
}

static void
http_client_try_response_direct(struct http_client *client)
{
    ssize_t nbytes;

    assert(client->fd >= 0);
    assert(client->response.read_state == READ_BODY);

    nbytes = http_body_try_direct(&client->response.body_reader,
                                  client->fd, client->fd_type);
    if (nbytes == -2 || nbytes == -3)
        /* either the destination fd blocks (-2) or the stream (and
           the whole connection) has been closed during the direct()
           callback (-3); no further checks */
        return;

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&client->event, EV_READ);
            return;
        }

        daemon_log(1, "http_client: read error (%s)\n", strerror(errno));
        stopwatch_event(client->stopwatch, "error");
        http_client_abort_response_body(client);
        return;
    }

    if (nbytes == 0)
        return;

    if (http_body_eof(&client->response.body_reader))
        http_client_response_stream_eof(client);
    else
        event2_or(&client->event, EV_READ);
}

static void
http_client_try_read_buffered(struct http_client *client)
{
    ssize_t nbytes;

    nbytes = recv_to_buffer(client->fd, client->input, INT_MAX);
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
        } else {
            daemon_log(2, "http_client: server closed connection "
                       "during response headers\n");
            stopwatch_event(client->stopwatch, "error");
            http_client_abort_response_headers(client);
        }

        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&client->event, EV_READ);
            return;
        }

        daemon_log(1, "http_client: read error (%s)\n", strerror(errno));
        stopwatch_event(client->stopwatch, "error");
        http_client_abort_response(client);
        return;
    }

    if (client->response.read_state == READ_BODY ||
        http_client_consume_headers(client)) {
        assert(client->response.body != NULL);

        if (client->fd >= 0 &&
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
    bool bret;

    assert(client->fd >= 0);

    if (client->response.read_state == READ_BODY &&
        (client->response.body_reader.output.handler_direct & client->fd_type) != 0) {
        if (!fifo_buffer_empty(client->input)) {
            /* there is still data in the body, which we have to
               consume before we do direct splice() */
            bret = http_client_consume_body(client);
            if (!bret || !fifo_buffer_empty(client->input))
                return;
        }

        http_client_try_response_direct(client);
    } else
        http_client_try_read_buffered(client);
}

static void
http_client_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct http_client *client = ctx;

    assert(client->fd >= 0);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "http_client: timeout\n");
        stopwatch_event(client->stopwatch, "timeout");
        http_client_abort_response(client);
        return;
    }

    pool_ref(client->pool);
    event2_lock(&client->event);
    event2_occurred_persist(&client->event, event);

    if ((event & EV_WRITE) != 0)
        istream_read(client->request.istream);

    if (client->fd >= 0 && (event & EV_READ) != 0)
        http_client_try_read(client);

    if (client->fd >= 0 && !fifo_buffer_full(client->input))
        event2_or(&client->event, EV_READ);

    event2_unlock(&client->event);
    pool_unref(client->pool);
    pool_commit();
}


/*
 * istream handler for the request
 *
 */

static size_t
http_client_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct http_client *client = ctx;
    ssize_t nbytes;

    assert(client->fd >= 0);

    nbytes = send(client->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (likely(nbytes >= 0)) {
        event2_or(&client->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&client->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "http_client: write error (%s)\n", strerror(errno));

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
    http_client_abort_response(client);
    return 0;
}

#ifdef __linux
static ssize_t
http_client_request_stream_direct(istream_direct_t type, int fd,
                                  size_t max_length, void *ctx)
{
    struct http_client *client = ctx;
    ssize_t nbytes;

    assert(client->fd >= 0);

    nbytes = istream_direct_to_socket(type, fd, client->fd, max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd_ready_for_writing(client->fd)) {
            event2_or(&client->event, EV_WRITE);
            return -2;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = istream_direct_to_socket(type, fd, client->fd, max_length);
    }

    if (likely(nbytes > 0))
        event2_or(&client->event, EV_WRITE);

    return nbytes;
}
#endif

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client *client = ctx;

    stopwatch_event(client->stopwatch, "request");

    client->request.istream = NULL;

    event2_set(&client->event, EV_READ);
}

static void
http_client_request_stream_abort(void *ctx)
{
    struct http_client *client = ctx;

    stopwatch_event(client->stopwatch, "abort");

    client->request.istream = NULL;

    http_client_abort_response(client);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
#ifdef __linux
    .direct = http_client_request_stream_direct,
#endif
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
http_client_request(pool_t caller_pool, int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    struct growing_buffer *headers,
                    istream_t body,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    pool_t pool;
    struct http_client *client;
    const char *p;
    istream_t request_line_stream, header_stream;
    static const struct timeval tv = {
        .tv_sec = 30,
        .tv_usec = 0,
    };

    assert(fd >= 0);
    assert(http_method_is_valid(method));
    assert(handler != NULL);
    assert(handler->response != NULL);

    if (!uri_verify_quick(uri)) {
        daemon_log(4, "http-client: malformed request URI '%s'\n", uri);
        http_response_handler_direct_abort(handler, ctx);
        return;
    }

    pool = pool_new_linear(caller_pool, "http_client_request", 8192);

    client = p_malloc(pool, sizeof(*client));
    client->stopwatch = stopwatch_fd_new(pool, fd, uri);
    client->pool = pool;
    client->fd = fd;
    client->fd_type = fd_type;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);

    client->response.read_state = READ_STATUS;
    client->response.no_body = method == HTTP_METHOD_HEAD;

    event2_init(&client->event, client->fd,
                http_client_event_callback, client,
                &tv);
    event2_persist(&client->event);

    client->input = fifo_buffer_new(client->pool, 4096);

    pool_ref(caller_pool);
    client->caller_pool = caller_pool;
    http_response_handler_set(&client->request.handler, handler, ctx);

    async_init(&client->request.async, &http_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    /* request line */

    p = p_strcat(client->pool,
                 http_method_to_string(method), " ", uri,
                 " HTTP/1.1\r\n", NULL);
    request_line_stream = istream_string_new(client->pool, p);

    /* headers */

    if (headers == NULL)
        headers = growing_buffer_new(client->pool, 256);

    if (body != NULL) {
        off_t content_length = istream_available(body, false);
        if (content_length == (off_t)-1) {
            header_write(headers, "transfer-encoding", "chunked");
            body = istream_chunked_new(client->pool, body);
        } else {
            snprintf(client->request.content_length_buffer,
                     sizeof(client->request.content_length_buffer),
                     "%lu", (unsigned long)content_length);
            header_write(headers, "content-length",
                         client->request.content_length_buffer);
        }
    }

    growing_buffer_write_buffer(headers, "\r\n", 2);

    header_stream = growing_buffer_istream(headers);

    /* request istream */

    client->request.istream = istream_cat_new(client->pool,
                                              request_line_stream,
                                              header_stream, body,
                                              NULL);

    istream_handler_set(client->request.istream,
                        &http_client_request_stream_handler, client,
                        istream_direct_mask_to(fd_type));

    pool_ref(pool);
    event2_lock(&client->event);
    event2_set(&client->event, EV_READ);

    istream_read(client->request.istream);

    event2_unlock(&client->event);
    pool_unref(pool);
}
