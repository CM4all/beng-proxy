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

#include <inline/compiler.h>
#include <inline/poison.h>
#include <daemon/log.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct http_client {
    pool_t pool, caller_pool;

    /* I/O */
    int fd;
    struct lease_ref lease_ref;
    struct event2 event;
    fifo_buffer_t input;

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
            READ_NONE,
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
        } read_state;
        bool http_1_1;
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
    return client->fd >= 0;
}

static bool
http_client_consume_body(struct http_client *client);

static void
http_client_try_read(struct http_client *client);



/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
http_client_release(struct http_client *client, bool reuse)
{
    assert(client != NULL);

    event2_set(&client->event, 0);
    event2_commit(&client->event);
    client->fd = -1;
    lease_release(&client->lease_ref, reuse);
    pool_unref(client->pool);
}

/**
 * Abort sending the request to the HTTP server.
 */
static void
http_client_abort_request(struct http_client *client)
{
    assert(client->response.read_state == READ_NONE);

    istream_close_handler(client->request.istream);

    http_response_handler_invoke_abort(&client->request.handler);
    pool_unref(client->caller_pool);

    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_headers(struct http_client *client)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    http_response_handler_invoke_abort(&client->request.handler);
    pool_unref(client->caller_pool);

    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
 */
static void
http_client_abort_response_body(struct http_client *client)
{
    assert(client->response.read_state == READ_BODY);

    istream_deinit_abort(&client->response.body_reader.output);
    http_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the HTTP server.
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
http_client_response_stream_available(istream_t istream,
                                      bool partial __attr_unused)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client != NULL);
    assert(client->fd >= 0);
    assert(client->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&client->request.handler));

    return http_body_available(&client->response.body_reader);
}

static void
http_client_response_stream_read(istream_t istream)
{
    struct http_client *client = response_stream_to_http_client(istream);
    bool bret;

    assert(client != NULL);
    assert(client->fd >= 0);
    assert(client->response.read_state == READ_BODY);
    assert(client->response.body_reader.output.handler != NULL);
    assert(!http_response_handler_defined(&client->request.handler));

    bret = http_client_consume_body(client);
    if (!bret)
        return;

    if (client->response.read_state == READ_BODY)
        http_client_try_read(client);
}

static void
http_client_response_stream_close(istream_t istream)
{
    struct http_client *client = response_stream_to_http_client(istream);

    assert(client->response.read_state == READ_BODY);
    assert(!http_response_handler_defined(&client->request.handler));
    assert(!http_body_eof(&client->response.body_reader));

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

    if (length > 4 && memcmp(line, "HTTP", 4) == 0) {
        client->response.http_1_1 = length >= 8 &&
            memcmp(line + 4, "/1.1", 4) == 0;

        space = memchr(line + 4, ' ', length - 4);
        if (space != NULL) {
            length -= space - line + 1;
            line = space + 1;
        }
    } else
        client->response.http_1_1 = false;

    if (unlikely(length < 3 || !char_is_digit(line[0]) ||
                 !char_is_digit(line[1]) || !char_is_digit(line[2]))) {
        daemon_log(2, "no HTTP status found\n");
        http_client_abort_response_headers(client);
        return false;
    }

    client->response.status = (http_status_t)(((line[0] - '0') * 10 + line[1] - '0') * 10 + line[2] - '0');
    if (unlikely(client->response.status < 100 || client->response.status > 599)) {
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

    header_connection = strmap_get(client->response.headers, "connection");
    client->keep_alive =
        (header_connection == NULL && client->response.http_1_1) ||
        (header_connection != NULL &&
         strcasecmp(header_connection, "keep-alive") == 0);

    if (http_status_is_empty(client->response.status)) {
        client->response.body = NULL;
        client->response.read_state = READ_BODY;
        return true;
    }

    transfer_encoding = strmap_remove(client->response.headers,
                                      "transfer-encoding");
    content_length_string = strmap_remove(client->response.headers,
                                          "content-length");

    if (transfer_encoding == NULL ||
        strcasecmp(transfer_encoding, "chunked") != 0) {
        /* not chunked */

        if (unlikely(content_length_string == NULL)) {
            if (client->keep_alive) {
                daemon_log(2, "no Content-Length header in HTTP response\n");
                http_client_abort_response_headers(client);
                return false;
            }
            content_length = (off_t)-1;
        } else {
            content_length = strtoul(content_length_string, &endptr, 10);
            if (unlikely(*endptr != 0 || content_length < 0)) {
                daemon_log(2, "invalid Content-Length header in HTTP response\n");
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

    if (!fifo_buffer_empty(client->input)) {
        daemon_log(2, "excess data after HTTP response\n");
        client->keep_alive = false;
    }

    http_client_release(client, client->keep_alive);
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
        if (likely(*end == '\r'))
            --end;
        while (unlikely(end >= start && char_is_whitespace(*end)))
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

    http_response_handler_invoke_response(&client->request.handler,
                                          client->response.status,
                                          client->response.headers,
                                          client->response.body);
    pool_unref(client->caller_pool);

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

    nbytes = http_body_try_direct(&client->response.body_reader, client->fd);
    if (nbytes < 0) {
        /* XXX EAGAIN? */
        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_client_abort_response_body(client);
        return;
    }

    if (nbytes > 0 && http_body_eof(&client->response.body_reader))
        http_client_response_stream_eof(client);
}

static void
http_client_try_read_buffered(struct http_client *client)
{
    ssize_t nbytes;

    nbytes = read_to_buffer(client->fd, client->input, INT_MAX);
    assert(nbytes != -2);

    if (nbytes == 0) {
        if (client->response.read_state == READ_BODY) {
            http_body_socket_eof(&client->response.body_reader,
                                 client->input);
            http_client_release(client, false);
        } else
            http_client_abort_response_headers(client);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&client->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on HTTP connection: %s\n", strerror(errno));
        http_client_abort_response(client);
        return;
    }

    if (client->response.read_state == READ_BODY)
        http_client_consume_body(client);
    else
        http_client_consume_headers(client);
}

static void
http_client_try_read(struct http_client *client)
{
    bool bret;

    if (client->response.read_state == READ_BODY &&
        (client->response.body_reader.output.handler_direct & ISTREAM_SOCKET) != 0) {
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

    event2_reset(&client->event);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        if (client->response.read_state == READ_NONE)
            http_client_abort_request(client);
        else
            http_client_abort_response(client);
        return;
    }

    pool_ref(client->pool);
    event2_lock(&client->event);

    if ((event & EV_WRITE) != 0)
        istream_read(client->request.istream);

    if (http_client_valid(client) && (event & EV_READ) != 0)
        http_client_try_read(client);

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

    nbytes = write(client->fd, data, length);
    if (likely(nbytes >= 0)) {
        event2_or(&client->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&client->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "write error on HTTP client connection: %s\n",
               strerror(errno));
    http_client_abort_request(client);
    return 0;
}

static void
http_client_request_stream_eof(void *ctx)
{
    struct http_client *client = ctx;

    client->response.read_state = READ_STATUS;
    client->input = fifo_buffer_new(client->pool, 4096);

    event2_set(&client->event, EV_READ);
}

static void
http_client_request_stream_abort(void *ctx)
{
    struct http_client *client = ctx;

    http_response_handler_invoke_abort(&client->request.handler);
    pool_unref(client->caller_pool);

    http_client_release(client, false);
}

static const struct istream_handler http_client_request_stream_handler = {
    .data = http_client_request_stream_data,
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
    
    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == READ_NONE ||
           client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    pool_unref(client->caller_pool);

    if (client->response.read_state == READ_NONE)
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
http_client_request(pool_t caller_pool, int fd,
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
    assert(handler != NULL);
    assert(handler->response != NULL);

    pool = pool_new_linear(caller_pool, "http_client_request", 8192);

    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->fd = fd;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);

    client->response.read_state = READ_NONE;

    event2_init(&client->event, client->fd,
                http_client_event_callback, client,
                &tv);

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
                        0);

    istream_read(client->request.istream);
}
