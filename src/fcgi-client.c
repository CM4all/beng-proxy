/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-client.h"
#include "fcgi-quark.h"
#include "fcgi-protocol.h"
#include "fcgi-serialize.h"
#include "buffered_socket.h"
#include "growing-buffer.h"
#include "http-response.h"
#include "async.h"
#include "istream-internal.h"
#include "istream-gb.h"
#include "please.h"
#include "strutil.h"
#include "header-parser.h"
#include "pevent.h"
#include "direct.h"
#include "fd-util.h"
#include "strmap.h"

#include <glib.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define macro_htons(s) ((int16_t)(s))
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN 
#define macro_htons(s) ((int16_t)((((s) >> 8) & 0xff) | (((s) << 8) & 0xff00)))
#else
#error Unknown byte order
#endif
#endif

struct fcgi_client {
    struct pool *pool, *caller_pool;

    struct buffered_socket socket;

    struct lease_ref lease_ref;

    struct http_response_handler_ref handler;
    struct async_operation async;

    uint16_t id;

    struct {
        struct istream *istream;

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;
    } request;

    struct {
        enum {
            READ_HEADERS,

            /**
             * There is no response body.  Waiting for the
             * #FCGI_END_REQUEST packet, and then we'll forward the
             * response to the #http_response_handler.
             */
            READ_NO_BODY,

            READ_BODY,
        } read_state;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        http_status_t status;

        struct strmap *headers;

        struct istream body;

        off_t available;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        bool no_body;

        /**
         * This flag is true if fcgi_client_submit_response() is
         * currently calling the HTTP response handler.  During this
         * period, fcgi_client_response_body_read() does nothing, to
         * prevent recursion.
         */
        bool in_handler;

        /**
         * Is the FastCGI application currently sending a STDERR
         * packet?
         */
        bool stderr;
    } response;

    size_t content_length, skip_length;
};

static const struct timeval fcgi_client_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
fcgi_client_response_body_init(struct fcgi_client *client);

/*
static void
fcgi_client_schedule_read(struct fcgi_client *client)
{
    assert(!fifo_buffer_full(client->input));

    socket_wrapper_schedule_read_timeout(&client->socket,
                                         client->request.istream != NULL
                                         ? NULL : &fcgi_client_timeout);
}

static void
fcgi_client_schedule_write(struct fcgi_client *client)
{
    assert(socket_wrapper_valid(&client->socket));

    socket_wrapper_schedule_write(&client->socket);
}
*/

/**
 * Release the socket held by this object.
 */
static void
fcgi_client_release_socket(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);

    buffered_socket_abandon(&client->socket);
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
fcgi_client_release(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);

    if (buffered_socket_connected(&client->socket))
        fcgi_client_release_socket(client, reuse);

    buffered_socket_destroy(&client->socket);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the FastCGI
 * server, and notify the HTTP response handler.
 */
static void
fcgi_client_abort_response_headers(struct fcgi_client *client, GError *error)
{
    assert(client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_NO_BODY);

    async_operation_finished(&client->async);

    fcgi_client_release_socket(client, false);

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    http_response_handler_invoke_abort(&client->handler, error);

    fcgi_client_release(client, false);
}

/**
 * Abort receiving the response body from the FastCGI server, and
 * notify the response body istream handler.
 */
static void
fcgi_client_abort_response_body(struct fcgi_client *client, GError *error)
{
    assert(client->response.read_state == READ_BODY);

    if (buffered_socket_connected(&client->socket))
        fcgi_client_release_socket(client, false);

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    istream_deinit_abort(&client->response.body, error);
    fcgi_client_release(client, false);
}

/**
 * Abort receiving the response from the FastCGI server.  This is a
 * wrapper for fcgi_client_abort_response_headers() or
 * fcgi_client_abort_response_body().
 */
static void
fcgi_client_abort_response(struct fcgi_client *client, GError *error)
{
    assert(client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_NO_BODY ||
           client->response.read_state == READ_BODY);

    if (client->response.read_state != READ_BODY)
        fcgi_client_abort_response_headers(client, error);
    else
        fcgi_client_abort_response_body(client, error);
}

/**
 * Close the response body.  This is a request from the istream
 * client, and we must not call it back according to the istream API
 * definition.
 */
static void
fcgi_client_close_response_body(struct fcgi_client *client)
{
    assert(client->response.read_state == READ_BODY);

    if (buffered_socket_connected(&client->socket))
        fcgi_client_release_socket(client, false);

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    istream_deinit(&client->response.body);
    fcgi_client_release(client, false);
}

static bool
fcgi_client_handle_line(struct fcgi_client *client,
                        const char *line, size_t length)
{
    assert(client != NULL);
    assert(client->response.headers != NULL);
    assert(line != NULL);

    if (length > 0) {
        header_parse_line(client->pool, client->response.headers,
                          line, length);
        return false;
    } else {
        client->response.read_state = READ_BODY;
        client->response.stderr = false;
        return true;
    }
}

static size_t
fcgi_client_parse_headers(struct fcgi_client *client,
                          const char *data, size_t length)
{
    const char *p = data, *const data_end = data + length;

    const char *next = NULL;
    bool finished = false;

    const char *eol;
    while ((eol = memchr(p, '\n', data_end - p)) != NULL) {
        next = eol + 1;
        --eol;
        while (eol >= p && char_is_whitespace(*eol))
            --eol;

        finished = fcgi_client_handle_line(client, p, eol - p + 1);
        if (finished)
            break;

        p = next;
    }

    return next != NULL ? next - data : 0;
}

/**
 * Feed data into the FastCGI protocol parser.
 *
 * @return the number of bytes consumed, or 0 if this object has been
 * destructed
 */
static size_t
fcgi_client_feed(struct fcgi_client *client,
                 const uint8_t *data, size_t length)
{
    if (client->response.stderr) {
        ssize_t nbytes = fwrite(data, 1, length, stderr);
        return nbytes > 0 ? (size_t)nbytes : 0;
    }

    switch (client->response.read_state) {
        size_t consumed;

    case READ_HEADERS:
        return fcgi_client_parse_headers(client, (const char *)data, length);

    case READ_NO_BODY:
        /* unreachable */
        assert(false);
        return 0;

    case READ_BODY:
        if (client->response.available == 0)
            /* discard following data */
            /* TODO: emit an error when that happens */
            return length;

        if (client->response.available > 0 &&
            (off_t)length > client->response.available)
            /* TODO: emit an error when that happens */
            length = client->response.available;

        consumed = istream_invoke_data(&client->response.body, data, length);
        if (consumed > 0 && client->response.available >= 0) {
            assert((off_t)consumed <= client->response.available);
            client->response.available -= consumed;
        }

        return consumed;
    }

    /* unreachable */
    assert(false);
    return 0;
}

/**
 * Submit the response metadata to the #http_response_handler.
 *
 * @return false if the connection was closed
 */
static bool
fcgi_client_submit_response(struct fcgi_client *client)
{
    assert(client->response.read_state == READ_BODY);

    http_status_t status = HTTP_STATUS_OK;

    const char *p = strmap_remove(client->response.headers,
                                  "status");
    if (p != NULL) {
        int i = atoi(p);
        if (http_status_is_valid(i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status) || client->response.no_body) {
        client->response.read_state = READ_NO_BODY;
        client->response.status = status;
        return true;
    }

    client->response.available = -1;
    p = strmap_remove(client->response.headers,
                      "content-length");
    if (p != NULL) {
        char *endptr;
        unsigned long long l = strtoull(p, &endptr, 10);
        if (endptr > p && *endptr == 0)
            client->response.available = l;
    }

    async_operation_finished(&client->async);

    fcgi_client_response_body_init(client);
    struct istream *body = body = istream_struct_cast(&client->response.body);

    struct pool *caller_pool = client->caller_pool;
    pool_ref(caller_pool);

    client->response.in_handler = true;
    http_response_handler_invoke_response(&client->handler, status,
                                          client->response.headers,
                                          body);
    client->response.in_handler = false;

    pool_unref(caller_pool);

    return buffered_socket_valid(&client->socket);
}

/**
 * Call this when you need more data to check whether this is
 * possible.  If the socket has been closed already, then the request
 * is aborted with an error.
 */
static bool
fcgi_client_check_more_data(struct fcgi_client *client)
{
    if (buffered_socket_connected(&client->socket))
        /* we can still receive more data from the server,
           everything's fine */
        return true;

    GError *error = g_error_new_literal(fcgi_quark(), 0,
                                        "premature disconnect "
                                        "from FastCGI application");
    fcgi_client_abort_response(client, error);
    return false;
}

/**
 * Handle an END_REQUEST packet.  This function will always destroy
 * the client.
 */
static void
fcgi_client_handle_end(struct fcgi_client *client, size_t remaining)
{
    if (client->response.read_state == READ_HEADERS) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "premature end of headers "
                                "from FastCGI application");
        fcgi_client_abort_response_headers(client, error);
        return;
    }

    if (buffered_socket_connected(&client->socket)) {
        /* if the socket is still alive at this point, release it, and
           allow reusing it only if the remaining buffer provides
           enough to finish the END_REQUEST payload */

        const size_t payload_length =
            client->content_length + client->skip_length;
        fcgi_client_release_socket(client,
                                   remaining == payload_length);
    }

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    if (client->response.read_state == READ_NO_BODY) {
        async_operation_finished(&client->async);
        http_response_handler_invoke_response(&client->handler,
                                              client->response.status,
                                              client->response.headers,
                                              NULL);
    } else
        istream_deinit_eof(&client->response.body);

    fcgi_client_release(client, false);
}

/**
 * A packet header was received.
 *
 * @param remaining the remaining length of the input buffer (not
 * including the header)
 * @return false if the client has been destroyed
 */
static bool
fcgi_client_handle_header(struct fcgi_client *client,
                          const struct fcgi_record_header *header,
                          size_t remaining)
{
    client->content_length = ntohs(header->content_length);
    client->skip_length = header->padding_length;

    if (header->request_id != client->id) {
        /* wrong request id; discard this packet */
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }

    if (client->response.read_state == READ_NO_BODY) {
        /* ignore all payloads until #FCGI_END_REQUEST */
        client->skip_length += client->content_length;
        client->content_length = 0;
    }

    switch (header->type) {
    case FCGI_STDOUT:
        client->response.stderr = false;
        return true;

    case FCGI_STDERR:
        client->response.stderr = true;
        return true;

    case FCGI_END_REQUEST:
        fcgi_client_handle_end(client, remaining);
        return false;

    default:
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }
}

/**
 * Consume data from the input buffer.
 *
 * @return false if the buffer is full or if this object has been
 * destructed
 */
static bool
fcgi_client_consume_input(struct fcgi_client *client,
                          const uint8_t *data0, size_t length0)
{
    const uint8_t *data = data0, *const end = data0 + length0;

    do {
        if (client->content_length > 0) {
            bool at_headers = client->response.read_state == READ_HEADERS;

            size_t length = end - data;
            if (length > client->content_length)
                length = client->content_length;

            size_t nbytes = fcgi_client_feed(client, data, length);
            if (nbytes == 0) {
                if (!at_headers)
                    return false;

                if (data == data0 && buffered_socket_full(&client->socket)) {
                    GError *error =
                        g_error_new_literal(fcgi_quark(), 0,
                                            "FastCGI response header too long");
                    fcgi_client_abort_response_headers(client, error);
                    return false;
                }

                return client->response.read_state == READ_BODY ||
                    fcgi_client_check_more_data(client);
            }

            data += nbytes;
            client->content_length -= nbytes;
            buffered_socket_consumed(&client->socket, nbytes);

            if (at_headers && client->response.read_state == READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */

                if (!fcgi_client_submit_response(client))
                    return false;

                /* continue parsing the response body from the
                   buffer */
                continue;
            }

            if (client->content_length > 0)
                break;

            continue;
        }

        if (client->skip_length > 0) {
            size_t nbytes = end - data;
            if (nbytes > client->skip_length)
                nbytes = client->skip_length;

            data += nbytes;
            client->skip_length -= nbytes;
            buffered_socket_consumed(&client->socket, nbytes);

            if (client->skip_length > 0)
                return true;

            continue;
        }

        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)data;
        const size_t remaining = end - data;
        if (remaining < sizeof(*header))
            return true;

        data += sizeof(*header);
        buffered_socket_consumed(&client->socket, sizeof(*header));

        if (!fcgi_client_handle_header(client, header, end - data))
            return false;
    } while (data != end);

    return true;
}

/*
 * istream handler for the request
 *
 */

static size_t
fcgi_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(buffered_socket_connected(&client->socket));
    assert(client->request.istream != NULL);

    client->request.got_data = true;

    ssize_t nbytes = buffered_socket_write(&client->socket, data, length);
    if (nbytes > 0)
        buffered_socket_schedule_write(&client->socket);
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            buffered_socket_schedule_write(&client->socket);
            return 0;
        }

        GError *error = g_error_new(fcgi_quark(), errno,
                                    "write to FastCGI application failed: %s",
                                    strerror(errno));
        fcgi_client_abort_response(client, error);
        return 0;
    }

    return (size_t)nbytes;
}

static ssize_t
fcgi_request_stream_direct(istream_direct_t type, int fd,
                           size_t max_length, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(buffered_socket_connected(&client->socket));

    client->request.got_data = true;

    ssize_t nbytes = buffered_socket_write_from(&client->socket, fd, type,
                                                max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!buffered_socket_ready_for_writing(&client->socket)) {
            buffered_socket_schedule_write(&client->socket);
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case client->fd has become ready between
           the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = buffered_socket_write_from(&client->socket, type, fd,
                                            max_length);
    }

    if (likely(nbytes > 0))
        buffered_socket_schedule_write(&client->socket);
    else if (nbytes < 0 && errno == EAGAIN) {
        client->request.got_data = false;
        buffered_socket_unschedule_write(&client->socket);
    }

    return nbytes;
}

static void
fcgi_request_stream_eof(void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    buffered_socket_unschedule_write(&client->socket);
}

static void
fcgi_request_stream_abort(GError *error, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    g_prefix_error(&error, "FastCGI request stream failed: ");
    fcgi_client_abort_response(client, error);
}

static const struct istream_handler fcgi_request_stream_handler = {
    .data = fcgi_request_stream_data,
    .direct = fcgi_request_stream_direct,
    .eof = fcgi_request_stream_eof,
    .abort = fcgi_request_stream_abort,
};


/*
 * istream implementation for the response body
 *
 */

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wextended-offsetof"
#endif

static inline struct fcgi_client *
response_stream_to_client(struct istream *istream)
{
    return (struct fcgi_client *)(((char*)istream) - offsetof(struct fcgi_client, response.body));
}

static off_t
fcgi_client_response_body_available(struct istream *istream, bool partial)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    if (client->response.available >= 0)
        return client->response.available;

    if (!partial)
        return -1;

    return client->content_length;
}

static void
fcgi_client_response_body_read(struct istream *istream)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    if (client->response.in_handler)
        /* avoid recursion; the http_response_handler caller will
           continue parsing the response if possible */
        return;

    buffered_socket_read(&client->socket);
}

static void
fcgi_client_response_body_close(struct istream *istream)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    fcgi_client_close_response_body(client);
}

static const struct istream_class fcgi_client_response_body = {
    .available = fcgi_client_response_body_available,
    .read = fcgi_client_response_body_read,
    .close = fcgi_client_response_body_close,
};

static void
fcgi_client_response_body_init(struct fcgi_client *client)
{
    istream_init(&client->response.body, &fcgi_client_response_body,
                 client->pool);
}

/*
 * socket_wrapper handler
 *
 */

static bool
fcgi_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct fcgi_client *client = ctx;

    pool_ref(client->pool);
    const bool result = fcgi_client_consume_input(client, buffer, size);
    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_closed(size_t remaining, void *ctx)
{
    struct fcgi_client *client = ctx;

    if (remaining > 0 &&
        /* only READ_BODY could have blocked */
        client->response.read_state == READ_BODY &&
        remaining >= client->content_length + client->skip_length) {
        /* the rest of the response may already be in the input
           buffer */
        fcgi_client_release_socket(client, false);
        return true;
    }

    GError *error =
        g_error_new_literal(fcgi_quark(), 0,
                            "FastCGI server closed the connection prematurely");
    fcgi_client_abort_response(client, error);
    return false;
}

static void
fcgi_client_socket_end(void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->response.read_state == READ_BODY);

    GError *error =
        g_error_new_literal(fcgi_quark(), 0,
                            "FastCGI server closed the connection prematurely");
    fcgi_client_abort_response_body(client, error);
}

static bool
fcgi_client_socket_write(void *ctx)
{
    struct fcgi_client *client = ctx;

    pool_ref(client->pool);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = buffered_socket_valid(&client->socket);
    if (result && client->request.istream != NULL) {
        if (client->request.got_data)
            buffered_socket_schedule_write(&client->socket);
        else
            buffered_socket_unschedule_write(&client->socket);
    }

    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_timeout(void *ctx)
{
    struct fcgi_client *client = ctx;

    GError *error = g_error_new_literal(fcgi_quark(), 0, "timeout");
    fcgi_client_abort_response(client, error);
    return false;
}

static void
fcgi_client_socket_error(GError *error, void *ctx)
{
    struct fcgi_client *client = ctx;

    fcgi_client_abort_response(client, error);
}

static const struct buffered_socket_handler fcgi_client_socket_handler = {
    .data = fcgi_client_socket_data,
    .closed = fcgi_client_socket_closed,
    .end = fcgi_client_socket_end,
    .write = fcgi_client_socket_write,
    .timeout = fcgi_client_socket_timeout,
    .error = fcgi_client_socket_error,
};

/*
 * async operation
 *
 */

static struct fcgi_client *
async_to_fcgi_client(struct async_operation *ao)
{
    return (struct fcgi_client*)(((char*)ao) - offsetof(struct fcgi_client, async));
}

static void
fcgi_client_request_abort(struct async_operation *ao)
{
    struct fcgi_client *client
        = async_to_fcgi_client(ao);
    
    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_NO_BODY);

    if (client->request.istream != NULL)
        istream_close_handler(client->request.istream);

    fcgi_client_release(client, false);
}

static const struct async_operation_class fcgi_client_async_operation = {
    .abort = fcgi_client_request_abort,
};


/*
 * constructor
 *
 */

void
fcgi_client_request(struct pool *caller_pool, int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    const char *script_filename,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    struct strmap *headers, struct istream *body,
                    const char *const params[], unsigned num_params,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    static unsigned next_request_id = 1;
    ++next_request_id;

    struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .request_id = GUINT16_TO_BE(next_request_id),
        .padding_length = 0,
        .reserved = 0,
    };
    static const struct fcgi_begin_request begin_request = {
        .role = macro_htons(FCGI_RESPONDER),
        .flags = FCGI_KEEP_CONN,
    };

    assert(http_method_is_valid(method));

    struct pool *pool = pool_new_linear(caller_pool, "fcgi_client_request",
                                        8192);
    struct fcgi_client *client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    buffered_socket_init(&client->socket, pool, fd, fd_type,
                         &fcgi_client_timeout, &fcgi_client_timeout,
                         &fcgi_client_socket_handler, client);

    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "fcgi_client_lease");

    http_response_handler_set(&client->handler, handler, handler_ctx);

    async_init(&client->async, &fcgi_client_async_operation);
    async_ref_set(async_ref, &client->async);

    client->id = header.request_id;

    client->response.read_state = READ_HEADERS;
    client->response.headers = strmap_new(client->caller_pool, 17);
    client->response.no_body = http_method_is_empty(method);
    client->content_length = 0;
    client->skip_length = 0;

    struct growing_buffer *buffer = growing_buffer_new(pool, 1024);
    header.type = FCGI_BEGIN_REQUEST;
    header.content_length = htons(sizeof(begin_request));
    growing_buffer_write_buffer(buffer, &header, sizeof(header));
    growing_buffer_write_buffer(buffer, &begin_request, sizeof(begin_request));

    fcgi_serialize_params(buffer, header.request_id,
                          "REQUEST_METHOD", http_method_to_string(method),
                          "REQUEST_URI", uri,
                          "SCRIPT_FILENAME", script_filename,
                          "SCRIPT_NAME", script_name,
                          "PATH_INFO", path_info,
                          "QUERY_STRING", query_string,
                          "DOCUMENT_ROOT", document_root,
                          "SERVER_SOFTWARE", "beng-proxy v" VERSION,
                          NULL);

    if (remote_addr != NULL)
        fcgi_serialize_params(buffer, header.request_id,
                              "REMOTE_ADDR", remote_addr,
                              NULL);

    off_t available = body != NULL
        ? istream_available(body, false)
        : -1;
    if (available >= 0) {
        char value[64];
        snprintf(value, sizeof(value),
                 "%lu", (unsigned long)available);

        const char *content_type = strmap_get_checked(headers, "content-type");

        fcgi_serialize_params(buffer, header.request_id,
                              "HTTP_CONTENT_LENGTH", value,
                              /* PHP wants the parameter without
                                 "HTTP_" */
                              "CONTENT_LENGTH", value,
                              /* same for the "Content-Type" request
                                 header */
                              content_type != NULL ? "CONTENT_TYPE" : NULL,
                              content_type,
                              NULL);
    }

    if (headers != NULL)
        fcgi_serialize_headers(buffer, header.request_id, headers);

    if (num_params > 0)
        fcgi_serialize_vparams(buffer, header.request_id, params, num_params);

    header.type = FCGI_PARAMS;
    header.content_length = htons(0);
    growing_buffer_write_buffer(buffer, &header, sizeof(header));

    struct istream *request;

    if (body != NULL)
        /* format the request body */
        request = istream_cat_new(pool,
                                  istream_gb_new(pool, buffer),
                                  istream_fcgi_new(pool, body,
                                                   header.request_id),
                                  NULL);
    else {
        /* no request body - append an empty STDIN packet */
        header.type = FCGI_STDIN;
        header.content_length = htons(0);
        growing_buffer_write_buffer(buffer, &header, sizeof(header));

        request = istream_gb_new(pool, buffer);
    }

    istream_assign_handler(&client->request.istream, request,
                           &fcgi_request_stream_handler, client,
                           istream_direct_mask_to(fd_type));

    buffered_socket_schedule_read_no_timeout(&client->socket);
    istream_read(client->request.istream);
}
