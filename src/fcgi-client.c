/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-client.h"
#include "fcgi-quark.h"
#include "fcgi-protocol.h"
#include "fcgi-serialize.h"
#include "socket_wrapper.h"
#include "growing-buffer.h"
#include "http-response.h"
#include "async.h"
#include "fifo-buffer.h"
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

    struct socket_wrapper socket;

    struct lease_ref lease_ref;

    struct http_response_handler_ref handler;
    struct async_operation async;

    uint16_t id;

    struct {
        struct istream *istream;
    } request;

    struct {
        enum {
            READ_HEADERS,
            READ_BODY,
            READ_DISCARD,
            READ_END
        } read_state;

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
         * Is the FastCGI application currently sending a STDERR
         * packet?
         */
        bool stderr;
    } response;

    struct fifo_buffer *input;
    size_t content_length, skip_length;
};

static const struct timeval fcgi_client_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
fcgi_client_response_body_init(struct fcgi_client *client);

static void
fcgi_client_schedule_read(struct fcgi_client *client)
{
    assert(socket_wrapper_valid(&client->socket));
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

/**
 * Release the socket held by this object.
 */
static void
fcgi_client_release_socket(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);
    assert(socket_wrapper_valid(&client->socket));

    socket_wrapper_abandon(&client->socket);
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

    if (socket_wrapper_valid(&client->socket))
        fcgi_client_release_socket(client, reuse);

    /* mark the fcgi_client object as "invalid" */
    client->input = NULL;

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
    assert(client->response.read_state == READ_HEADERS);

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

    if (socket_wrapper_valid(&client->socket))
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

    if (socket_wrapper_valid(&client->socket))
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
    const char *p, *data_end, *eol, *next = NULL;
    bool finished = false;

    p = data;
    data_end = data + length;

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

    case READ_END:
        assert(false);
        break;

    case READ_HEADERS:
        return fcgi_client_parse_headers(client, (const char *)data, length);

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

    case READ_DISCARD:
        return length;
    }

    /* unreachable */
    return 0;
}

/**
 * Consume data from the input buffer.
 *
 * @return false if the buffer is full or if this object has been
 * destructed
 */
static bool
fcgi_client_consume_input(struct fcgi_client *client)
{
    const void *data;
    size_t length;
    const struct fcgi_record_header *header;

    while (true) {
        data = fifo_buffer_read(client->input, &length);
        if (data == NULL)
            return true;

        if (client->content_length > 0) {
            bool at_headers = client->response.read_state == READ_HEADERS;
            size_t nbytes;

            if (length > client->content_length)
                length = client->content_length;

            nbytes = fcgi_client_feed(client, data, length);
            if (nbytes == 0) {
                if (!at_headers)
                    return false;

                if (fifo_buffer_full(client->input)) {
                    GError *error =
                        g_error_new_literal(fcgi_quark(), 0,
                                            "FastCGI response header too long");
                    fcgi_client_abort_response_headers(client, error);
                    return false;
                }

                return true;
            }

            fifo_buffer_consume(client->input, nbytes);
            length -= nbytes;
            client->content_length -= nbytes;

            if (at_headers && client->response.read_state == READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */

                async_operation_finished(&client->async);

                http_status_t status = HTTP_STATUS_OK;

                const char *p = strmap_remove(client->response.headers,
                                              "status");
                if (p != NULL) {
                    int i = atoi(p);
                    if (http_status_is_valid(i))
                        status = (http_status_t)i;
                }

                if (http_status_is_empty(status) ||
                    client->response.no_body) {
                    client->response.available = 0;
                } else {
                    client->response.available = -1;
                    p = strmap_remove(client->response.headers,
                                      "content-length");
                    if (p != NULL) {
                        char *endptr;
                        unsigned long long l = strtoull(p, &endptr, 10);
                        if (endptr > p && *endptr == 0)
                            client->response.available = l;
                    }
                }

                struct istream *body;
                if (!http_status_is_empty(status) &&
                    !client->response.no_body) {
                    fcgi_client_response_body_init(client);
                    body = istream_struct_cast(&client->response.body);
                } else {
                    body = NULL;
                    client->response.read_state = READ_DISCARD;
                }

                struct pool *caller_pool = client->caller_pool;
                pool_ref(caller_pool);

                http_response_handler_invoke_response(&client->handler, status,
                                                      client->response.headers,
                                                      body);

                pool_unref(caller_pool);

                if (body == NULL) {
                    /* XXX when there is no response body, we cannot
                       finish reading the response here - we would
                       have to do that in background.  This is
                       complicated to implement, and until that is
                       done, we just bail out */
                    fcgi_client_release(client, false);
                    return false;
                }

                if (client->input == NULL)
                    /* response body was closed */
                    return false;
            }

            if (client->content_length > 0)
                return true;

            if (client->response.read_state == READ_END) {
                /* reuse the socket only if the remaining buffer
                   length is exactly the padding (which is very
                   likely) */
                fcgi_client_release(client, length == client->skip_length);
                return false;
            }

            continue;
        }

        if (client->skip_length > 0) {
            if (length > client->skip_length)
                length = client->skip_length;
            fifo_buffer_consume(client->input, length);
            client->skip_length -= length;

            if (client->skip_length > 0)
                return true;

            if (client->response.read_state == READ_END) {
                fcgi_client_release(client, fifo_buffer_empty(client->input));
                return false;
            }

            continue;
        }

        if (length < sizeof(*header))
            return true;

        header = data;

        if (header->request_id != client->id) {
            /* wrong request id; discard this packet */
            client->skip_length =
                ntohs(header->content_length) + header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));
            continue;
        }

        switch (header->type) {
        case FCGI_STDOUT:
            client->content_length = ntohs(header->content_length);
            client->skip_length = header->padding_length;
            client->response.stderr = false;
            fifo_buffer_consume(client->input, sizeof(*header));
            break;

        case FCGI_STDERR:
            client->content_length = ntohs(header->content_length);
            client->skip_length = header->padding_length;
            client->response.stderr = true;
            fifo_buffer_consume(client->input, sizeof(*header));
            break;

        case FCGI_END_REQUEST:
            if (client->response.read_state == READ_HEADERS) {
                GError *error =
                    g_error_new_literal(fcgi_quark(), 0,
                                        "premature end of headers "
                                        "from FastCGI application");
                fcgi_client_abort_response_headers(client, error);
                return false;
            }

            client->skip_length = ntohs(header->content_length) + header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));
            length -= sizeof(*header);

            fcgi_client_release_socket(client, length == client->skip_length);

            if (client->request.istream != NULL)
                istream_close_handler(client->request.istream);

            istream_deinit_eof(&client->response.body);
            client->response.read_state = READ_END;

            fcgi_client_release(client, false);
            return false;

        default:
            client->skip_length = ntohs(header->content_length) + header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));
            break;
        }
    }
}


/*
 * socket i/o
 *
 */

/**
 * Read data from the FastCGI connection into the input buffer and
 * attempt to consume it.  The buffer must not be full already.
 */
static bool
fcgi_client_try_read(struct fcgi_client *client)
{
    ssize_t nbytes;

    nbytes = socket_wrapper_read_to_buffer(&client->socket,
                                           client->input, 4096);
    assert(nbytes != -2);

    if (nbytes == 0) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "FastCGI server closed the connection");
        fcgi_client_abort_response(client, error);
        return false;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            fcgi_client_schedule_read(client);
            return true;
        }

        GError *error =
            g_error_new(fcgi_quark(), errno,
                        "read error on FastCGI connection: %s",
                        strerror(errno));
        fcgi_client_abort_response(client, error);
        return false;
    }

    if (fcgi_client_consume_input(client))
        fcgi_client_schedule_read(client);

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

    assert(socket_wrapper_valid(&client->socket));
    assert(client->request.istream != NULL);

    ssize_t nbytes = socket_wrapper_write(&client->socket, data, length);
    if (nbytes > 0)
        fcgi_client_schedule_write(client);
    else if (nbytes < 0) {
        if (errno == EAGAIN) {
            fcgi_client_schedule_write(client);
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

    assert(socket_wrapper_valid(&client->socket));

    ssize_t nbytes = socket_wrapper_write_from(&client->socket, fd, type,
                                               max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!socket_wrapper_ready_for_writing(&client->socket)) {
            fcgi_client_schedule_write(client);
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case client->fd has become ready between
           the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = socket_wrapper_write_from(&client->socket, type, fd,
                                           max_length);
    }

    if (likely(nbytes > 0))
        fcgi_client_schedule_write(client);

    return nbytes;
}

static void
fcgi_request_stream_eof(void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    socket_wrapper_unschedule_write(&client->socket);
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

    /* cancel any scheduled read event before doing anything else,
       just in case this function fills the buffer completely; if not,
       the read will be re-scheduled anyway */
    socket_wrapper_unschedule_read(&client->socket);

    if (fcgi_client_consume_input(client))
        fcgi_client_try_read(client);
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
fcgi_client_socket_write(void *ctx)
{
    struct fcgi_client *client = ctx;

    pool_ref(client->pool);

    istream_read(client->request.istream);

    bool result = socket_wrapper_valid(&client->socket);
    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_read(void *ctx)
{
    struct fcgi_client *client = ctx;

    pool_ref(client->pool);

    fcgi_client_try_read(client);

    bool result = socket_wrapper_valid(&client->socket);
    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_timeout(void *ctx)
{
    struct fcgi_client *client = ctx;

    GError *error = g_error_new_literal(fcgi_quark(), 0,
                                        "receive timeout");
    fcgi_client_abort_response(client, error);
    return false;
}

static const struct socket_handler fcgi_client_socket_handler = {
    .read = fcgi_client_socket_read,
    .write = fcgi_client_socket_write,
    .timeout = fcgi_client_socket_timeout,
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
    assert(client->response.read_state == READ_HEADERS);

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
    struct pool *pool;
    struct fcgi_client *client;

    assert(http_method_is_valid(method));

    pool = pool_new_linear(caller_pool, "fcgi_client_request", 8192);
    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    socket_wrapper_init(&client->socket, pool, fd, fd_type,
                        NULL, &fcgi_client_timeout,
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
    client->input = fifo_buffer_new(pool, 4096);
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

    fcgi_client_schedule_read(client);
    istream_read(client->request.istream);
}
