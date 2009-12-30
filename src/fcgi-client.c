/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-client.h"
#include "fcgi-protocol.h"
#include "growing-buffer.h"
#include "http-response.h"
#include "async.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "lease.h"
#include "strutil.h"
#include "header-parser.h"

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
    struct event event;

    struct http_response_handler_ref handler;
    struct async_operation async;

    struct growing_buffer *request;

    struct {
        enum {
            READ_NONE,
            READ_STATUS,
            READ_HEADERS,
            READ_BODY,
            READ_END
        } read_state;

        struct strmap *headers;

        struct istream body;
    } response;

    struct fifo_buffer *input;
    size_t content_length, skip_length;
};

static void
fcgi_client_event(int fd, short event, void *ctx);

static void
fcgi_client_response_body_init(struct fcgi_client *client);

/**
 * Release the socket held by this object.
 */
static void
fcgi_client_release_socket(struct fcgi_client *client, bool reuse)
{
    assert(client != NULL);
    assert(client->fd >= 0);

    event_del(&client->event);
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
    switch (client->response.read_state) {
    case READ_NONE:
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
        switch (header->type) {
        case FCGI_STDOUT:
            client->content_length = ntohs(header->content_length);
            client->skip_length = header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));
            break;

        case FCGI_END_REQUEST:
            client->skip_length = ntohs(header->content_length) + header->padding_length;
            fifo_buffer_consume(client->input, sizeof(*header));

            if (client->skip_length == 0)
                fcgi_client_release_socket(client,
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

static void
fcgi_client_try_read(struct fcgi_client *client)
{
    ssize_t nbytes;

    nbytes = recv_to_buffer(client->fd, client->input, 4096);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "FastCGI server closed the connection\n");
        fcgi_client_abort_response(client);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event_add(&client->event, NULL);
            return;
        }

        daemon_log(1, "read error on FastCGI connection: %s\n", strerror(errno));
        fcgi_client_abort_response(client);
        return;
    }

    if (fcgi_client_consume_input(client)) {
        assert(!fifo_buffer_full(client->input));
        event_add(&client->event, NULL);
    }
}

static void
fcgi_client_try_write(struct fcgi_client *client)
{
    const void *p;
    size_t length;
    ssize_t nbytes;

    p = growing_buffer_read(client->request, &length);
    if (p == NULL) {
        client->response.read_state = READ_HEADERS;
        client->response.headers = strmap_new(client->pool, 17);
        client->content_length = 0;
        client->skip_length = 0;

        event_set(&client->event, client->fd, EV_READ,
                  fcgi_client_event, client);
        event_add(&client->event, NULL);
        return;
    }

    nbytes = send(client->fd, p, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        daemon_log(3, "write to FastCGI application failed: %s\n",
                   strerror(errno));
        http_response_handler_invoke_abort(&client->handler);
        fcgi_client_release(client, false);
        return;
    }

    if (nbytes > 0)
        growing_buffer_consume(client->request, (size_t)nbytes);

    event_add(&client->event, NULL);
}


/*
 * libevent callback
 *
 */

static void
fcgi_client_event(int fd __attr_unused, short event, void *ctx)
{
    struct fcgi_client *client = ctx;

    if ((event & EV_WRITE) != 0)
        fcgi_client_try_write(client);
    else
        fcgi_client_try_read(client);

    pool_commit();
}


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
    assert(client->response.read_state == READ_NONE ||
           client->response.read_state == READ_STATUS ||
           client->response.read_state == READ_HEADERS);

    /* XXX close request body */

    fcgi_client_release(client, false);
}

static const struct async_operation_class fcgi_client_async_operation = {
    .abort = fcgi_client_request_abort,
};


/*
 * growing_buffer utilities
 *
 */

static size_t
gb_append_length(struct growing_buffer *gb, size_t length)
{
    if (length < 0x80) {
        uint8_t buffer = (uint8_t)length;
        growing_buffer_write_buffer(gb, &buffer, sizeof(buffer));
        return sizeof(buffer);
    } else {
        /* XXX 31 bit overflow? */
        uint32_t buffer = htonl(length | 0x80000000);
        growing_buffer_write_buffer(gb, &buffer, sizeof(buffer));
        return sizeof(buffer);
    }
}

static size_t
gb_append_pair(struct growing_buffer *gb, const char *name,
               const char *value)
{
    size_t size, name_length, value_length;

    assert(name != NULL);

    if (value == NULL)
        value = "";

    name_length = strlen(name);
    value_length = strlen(value);
    size = gb_append_length(gb, name_length) +
        gb_append_length(gb, value_length);

    growing_buffer_write_buffer(gb, name, name_length);
    growing_buffer_write_buffer(gb, value, value_length);

    return size + name_length + value_length;
}

static void
gb_append_params(struct growing_buffer *gb, const char *name,
               const char *value)
{
    struct fcgi_record_header *header;
    size_t content_length;

    header = growing_buffer_write(gb, sizeof(*header));
    header->version = FCGI_VERSION_1;
    header->type = FCGI_PARAMS;
    header->request_id = macro_htons(1);
    header->padding_length = 0;
    header->reserved = 0;

    content_length = gb_append_pair(gb, name, value);
    header->content_length = htons(content_length);
}


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
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct fcgi_record_header header = {
        .version = FCGI_VERSION_1,
        .request_id = macro_htons(1),
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
    event_set(&client->event, fd, EV_WRITE, fcgi_client_event, client);

    http_response_handler_set(&client->handler, handler, handler_ctx);

    async_init(&client->async, &fcgi_client_async_operation);
    async_ref_set(async_ref, &client->async);

    client->request = growing_buffer_new(pool, 1024);
    client->response.read_state = READ_NONE;
    client->input = fifo_buffer_new(pool, 4096);

    header.type = FCGI_BEGIN_REQUEST;
    header.content_length = htons(sizeof(begin_request));
    growing_buffer_write_buffer(client->request, &header, sizeof(header));
    growing_buffer_write_buffer(client->request, &begin_request, sizeof(begin_request));

    gb_append_params(client->request, "REQUEST_METHOD",
                     http_method_to_string(method));
    gb_append_params(client->request, "REQUEST_URI", uri);
    gb_append_params(client->request, "SCRIPT_FILENAME", script_filename);
    gb_append_params(client->request, "SCRIPT_NAME", script_name);
    gb_append_params(client->request, "PATH_INFO", path_info);
    gb_append_params(client->request, "QUERY_STRING", query_string);
    gb_append_params(client->request, "DOCUMENT_ROOT", document_root);
    gb_append_params(client->request, "SERVER_SOFTWARE", "beng-proxy v" VERSION);

    header.type = FCGI_STDIN;
    header.content_length = htons(0);
    growing_buffer_write_buffer(client->request, &header, sizeof(header));

    (void)headers; /* XXX */
    (void)body; /* XXX */

    fcgi_client_try_write(client);
}
