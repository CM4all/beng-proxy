/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-client.h"
#include "fcgi-protocol.h"
#include "fcgi-serialize.h"
#include "growing-buffer.h"
#include "http-response.h"
#include "async.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "lease.h"
#include "strutil.h"
#include "header-parser.h"
#include "pevent.h"

#include <glib.h>

#include <daemon/log.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <event.h>

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
    pool_t pool, caller_pool;

    int fd;
    struct lease_ref lease_ref;

    struct http_response_handler_ref handler;
    struct async_operation async;

    uint16_t id;

    struct {
        struct event event;

        istream_t istream;
    } request;

    struct {
        struct event event;

        enum {
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
            READ_END
        } read_state;

        struct strmap *headers;

        struct istream body;

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
    assert(client->fd >= 0);

    p_event_add(&client->response.event,
                client->request.istream != NULL
                ? NULL : &fcgi_client_timeout,
                client->pool, "fcgi_client_response");
}

static void
fcgi_client_schedule_write(struct fcgi_client *client)
{
    assert(client->fd >= 0);

    p_event_add(&client->request.event, &fcgi_client_timeout,
                client->pool, "fcgi_client_request");
}

/**
 * Release the socket held by this object.
 */
static void
fcgi_client_release_socket(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);
    assert(client->fd >= 0);

    p_event_del(&client->request.event, client->pool);
    p_event_del(&client->response.event, client->pool);

    client->fd = -1;
    lease_release(&client->lease_ref, reuse);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
fcgi_client_release(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);

    if (client->fd >= 0)
        fcgi_client_release_socket(client, reuse);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

/**
 * Abort receiving the response status/headers from the FastCGI
 * server.
 */
static void
fcgi_client_abort_response_headers(struct fcgi_client *client)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    fcgi_client_release_socket(client, false);

    http_response_handler_invoke_abort(&client->handler);

    fcgi_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the FastCGI
 * server.
 */
static void
fcgi_client_abort_response_body(struct fcgi_client *client)
{
    assert(client->response.read_state == READ_BODY);

    if (client->fd >= 0)
        fcgi_client_release_socket(client, false);

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    istream_deinit_abort(&client->response.body);
    fcgi_client_release(client, false);
}

/**
 * Abort receiving the response status/headers from the FastCGI
 * server.
 */
static void
fcgi_client_abort_response(struct fcgi_client *client)
{
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS ||
           client->response.read_state == READ_BODY);

    if (client->response.read_state != READ_BODY)
        fcgi_client_abort_response_headers(client);
    else
        fcgi_client_abort_response_body(client);
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

static size_t
fcgi_client_feed(struct fcgi_client *client, const char *data, size_t length)
{
    if (client->response.stderr) {
        ssize_t nbytes = fwrite(data, 1, length, stderr);
        return nbytes > 0 ? (size_t)nbytes : 0;
    }

    switch (client->response.read_state) {
    case READ_STATUS:
    case READ_END:
        assert(false);
        break;

    case READ_HEADERS:
        return fcgi_client_parse_headers(client, data, length);

    case READ_BODY:
        return istream_invoke_data(&client->response.body, data, length);
    }

    /* unreachable */
    return 0;
}

/**
 * Consume data from the input buffer.  Returns false if the buffer is
 * full or if this object has been destructed.
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
            if (nbytes == 0)
                return at_headers && !fifo_buffer_full(client->input);

            fifo_buffer_consume(client->input, nbytes);
            client->content_length -= nbytes;

            if (at_headers && client->response.read_state == READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */
                fcgi_client_response_body_init(client);
                http_response_handler_invoke_response(&client->handler,
                                                      HTTP_STATUS_OK,
                                                      client->response.headers,
                                                      istream_struct_cast(&client->response.body));
                return false;
            }

            if (client->content_length > 0)
                return true;

            if (client->response.read_state == READ_END &&
                client->skip_length == 0) {
                fcgi_client_release(client, fifo_buffer_empty(client->input));
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

        if (header->request_id != client->id)
            /* wrong request id; discard this packet */
            continue;

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
            if (client->response.read_state == READ_STATUS ||
                client->response.read_state == READ_HEADERS) {
                daemon_log(1, "premature end of headers "
                           "from FastCGI application\n");
                fcgi_client_abort_response(client);
                return false;
            }

            client->skip_length = ntohs(header->content_length) + header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));

            if (client->request.istream != NULL)
                istream_close_handler(client->request.istream);

            if (client->skip_length == 0)
                fcgi_client_release_socket(client,
                                           client->request.istream == NULL &&
                                           fifo_buffer_empty(client->input));

            istream_deinit_eof(&client->response.body);
            client->response.read_state = READ_END;

            if (client->skip_length == 0) {
                fcgi_client_release(client, false);
                return false;
            }

            break;

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

static bool
fcgi_client_try_read(struct fcgi_client *client)
{
    ssize_t nbytes;

    nbytes = recv_to_buffer(client->fd, client->input, 4096);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "FastCGI server closed the connection\n");
        fcgi_client_abort_response(client);
        return false;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            fcgi_client_schedule_read(client);
            return true;
        }

        daemon_log(1, "read error on FastCGI connection: %s\n", strerror(errno));
        fcgi_client_abort_response(client);
        return false;
    }

    if (fcgi_client_consume_input(client)) {
        assert(!fifo_buffer_full(client->input));
        fcgi_client_schedule_read(client);
    }

    return true;
}

static ssize_t
fcgi_client_send(struct fcgi_client *client, const void *data, size_t length)
{
    ssize_t nbytes = send(client->fd, data, length,
                          MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes >= 0)
        return nbytes;
    else if (errno == EAGAIN) {
        fcgi_client_schedule_write(client);
        return 0;
    } else {
        daemon_log(3, "write to FastCGI application failed: %s\n",
                   strerror(errno));
        fcgi_client_abort_response(client);
        return -1;
    }
}


/*
 * libevent callback
 *
 */

static void
fcgi_client_send_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->fd >= 0);

    p_event_consumed(&client->request.event, client->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "fcgi_client: send timeout\n");
        http_response_handler_invoke_abort(&client->handler);
        fcgi_client_release(client, false);
        return;
    }

    istream_read(client->request.istream);

    pool_commit();
}

static void
fcgi_client_recv_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->fd >= 0);

    p_event_consumed(&client->response.event, client->pool);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "fcgi_client: send timeout\n");
        fcgi_client_abort_response(client);
        return;
    }

    fcgi_client_try_read(client);

    pool_commit();
}


/*
 * istream handler for the request
 *
 */

static size_t
fcgi_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->fd >= 0);
    assert(client->request.istream != NULL);

    ssize_t nbytes = fcgi_client_send(client, data, length);
    if (nbytes < 0)
        return 0;

    return nbytes;
}

static void
fcgi_request_stream_eof(void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    p_event_del(&client->request.event, client->pool);
    fcgi_client_schedule_read(client);
}

static void
fcgi_request_stream_abort(void *ctx)
{
    struct fcgi_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    fcgi_client_abort_response(client);
}

static const struct istream_handler fcgi_request_stream_handler = {
    .data = fcgi_request_stream_data,
    .eof = fcgi_request_stream_eof,
    .abort = fcgi_request_stream_abort,
};


/*
 * istream implementation for the response body
 *
 */

static inline struct fcgi_client *
response_stream_to_client(istream_t istream)
{
    return (struct fcgi_client *)(((char*)istream) - offsetof(struct fcgi_client, response.body));
}

static off_t
fcgi_client_response_body_available(istream_t istream, bool partial)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    /* XXX optimize this */

    if (!partial)
        return -1;

    return client->content_length;
}

static void
fcgi_client_response_body_read(istream_t istream)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    if (fcgi_client_consume_input(client))
        fcgi_client_try_read(client);
}

static void
fcgi_client_response_body_close(istream_t istream)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    fcgi_client_abort_response_body(client);
}

static const struct istream fcgi_client_response_body = {
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
    assert(client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

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
fcgi_client_request(pool_t caller_pool, int fd,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    const char *script_filename,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    struct strmap *headers, istream_t body,
                    const char *const params[], unsigned num_params,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    off_t available = -1;
    if (body != NULL) {
        available = istream_available(body, false);
        if (available == -1) {
            daemon_log(1, "Unknown FastCGI request body length\n");
            http_response_handler_direct_abort(handler, handler_ctx);
            return;
        }
    }

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
    pool_t pool;
    struct fcgi_client *client;

    assert(http_method_is_valid(method));

    pool = pool_new_linear(caller_pool, "fcgi_client_request", 8192);
    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    client->fd = fd;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);

    event_set(&client->request.event, client->fd, EV_WRITE|EV_TIMEOUT,
              fcgi_client_send_event_callback, client);
    event_set(&client->response.event, client->fd, EV_READ|EV_TIMEOUT,
              fcgi_client_recv_event_callback, client);

    http_response_handler_set(&client->handler, handler, handler_ctx);

    async_init(&client->async, &fcgi_client_async_operation);
    async_ref_set(async_ref, &client->async);

    client->id = header.request_id;

    client->response.read_state = READ_HEADERS;
    client->response.headers = strmap_new(client->pool, 17);
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

    if (available >= 0) {
        char value[64];
        snprintf(value, sizeof(value),
                 "%lu", (unsigned long)available);

        const char *content_type = strmap_get(headers, "content-type");

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

    istream_t request;

    if (body != NULL)
        /* format the request body */
        request = istream_cat_new(pool,
                                  growing_buffer_istream(buffer),
                                  istream_fcgi_new(pool, body,
                                                   header.request_id),
                                  NULL);
    else {
        /* no request body - append an empty STDIN packet */
        header.type = FCGI_STDIN;
        header.content_length = htons(0);
        growing_buffer_write_buffer(buffer, &header, sizeof(header));

        request = growing_buffer_istream(buffer);
    }

    istream_assign_handler(&client->request.istream, request,
                           &fcgi_request_stream_handler, client,
                           0);

    fcgi_client_schedule_read(client);
    istream_read(client->request.istream);
}
