/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-client.h"
#include "ajp-headers.h"
#include "http-response.h"
#include "async.h"
#include "fifo-buffer.h"
#include "growing-buffer.h"
#include "event2.h"
#include "format.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "ajp-protocol.h"
#include "ajp-serialize.h"
#include "serialize.h"
#include "strref.h"
#include "lease.h"

#include <daemon/log.h>
#include <socket/util.h>

#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct ajp_client {
    pool_t pool;

    /* I/O */
    int fd;
    struct lease_ref lease_ref;
    struct event2 event;

    /* request */
    struct {
        istream_t istream;

        /** an istream_ajp_body */
        istream_t ajp_body;

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

    /* response */
    struct {
        enum {
            READ_BEGIN,
            READ_BODY,
            READ_END,
        } read_state;

        /**
         * This flag is true if ajp_consume_send_headers() is
         * currently calling the HTTP response handler.  During this
         * period, istream_ajp_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        fifo_buffer_t input;
        http_status_t status;
        struct strmap *headers;
        struct istream body;
        size_t chunk_length, junk_length;
    } response;
};

static const struct ajp_header empty_body_chunk = {
    .a = 0x12, .b = 0x34,
};

static inline bool
ajp_connection_valid(struct ajp_client *client)
{
    return client->fd >= 0;
}

static void
ajp_consume_input(struct ajp_client *client);

static void
ajp_try_read(struct ajp_client *client);


/**
 * Release resources held by this object: the event object, the socket
 * lease, the request body and the pool reference.
 */
static void
ajp_client_release(struct ajp_client *client, bool reuse)
{
    assert(client != NULL);
    assert(client->fd >= 0);

    event2_set(&client->event, 0);
    event2_commit(&client->event);
    client->fd = -1;

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    lease_release(&client->lease_ref, reuse);
    pool_unref(client->pool);
}

static void
ajp_connection_close(struct ajp_client *client)
{
    if (client->fd >= 0) {
        pool_ref(client->pool);

        switch (client->response.read_state) {
        case READ_BEGIN:
            http_response_handler_invoke_abort(&client->request.handler);
            client->response.read_state = READ_END;
            break;

        case READ_BODY:
            istream_deinit_abort(&client->response.body);
            client->response.read_state = READ_END;
            break;

        case READ_END:
            break;
        }

        if (client->fd >= 0)
            ajp_client_release(client, false);

        pool_unref(client->pool);
    }
}


/*
 * response body stream
 *
 */

static inline struct ajp_client *
istream_to_ajp(istream_t istream)
{
    return (struct ajp_client *)(((char*)istream) - offsetof(struct ajp_client, response.body));
}

static void
istream_ajp_read(istream_t istream)
{
    struct ajp_client *client = istream_to_ajp(istream);

    assert(client->response.read_state == READ_BODY);

    if (client->response.in_handler)
        return;

    if (fifo_buffer_full(client->response.input))
        ajp_consume_input(client);
    else
        ajp_try_read(client);
}

static void
istream_ajp_close(istream_t istream)
{
    struct ajp_client *client = istream_to_ajp(istream);

    assert(client->response.read_state == READ_BODY);

    ajp_client_release(client, false);
    istream_deinit_abort(&client->response.body);
}

static const struct istream ajp_response_body = {
    /* XXX .available */
    .read = istream_ajp_read,
    .close = istream_ajp_close,
};


/*
 * request generator
 *
 */

static size_t
ajp_write(struct ajp_client *client, const void *data, size_t length)
{
    ssize_t nbytes;

    assert(client != NULL);
    assert(client->fd >= 0);
    assert(data != NULL);
    assert(length > 0);

    nbytes = write(client->fd, data, length);
    if (nbytes < 0) {
        daemon_log(1, "write error on AJP client connection: %s\n",
                   strerror(errno));
        ajp_connection_close(client);
        return 0;
    }

    event2_or(&client->event, EV_WRITE);
    return (size_t)nbytes;
}


/*
 * response parser
 *
 */

static bool
ajp_consume_send_headers(struct ajp_client *client,
                         const char *data, size_t length)
{
    http_status_t status;
    unsigned num_headers;
    istream_t body;
    struct strref packet;
    struct strmap *headers;

    if (client->response.read_state != READ_BEGIN) {
        daemon_log(1, "unexpected SEND_HEADERS packet from AJP server\n");
        ajp_connection_close(client);
        return false;
    }

    strref_set(&packet, data, length);
    status = deserialize_uint16(&packet);
    deserialize_ajp_string(&packet);
    num_headers = deserialize_uint16(&packet);

    if (num_headers > 0) {
        headers = strmap_new(client->pool, 17);
        deserialize_ajp_headers(client->pool, headers, &packet, num_headers);
    } else
        headers = NULL;

    if (strref_is_null(&packet)) {
        daemon_log(1, "malformed SEND_HEADERS packet from AJP server\n");
        ajp_connection_close(client);
        return false;
    }

    if (!http_status_is_valid(status)) {
        daemon_log(1, "invalid status %u from AJP server\n", status);
        ajp_connection_close(client);
        return false;
    }

    if (http_status_is_empty(status)) {
        body = NULL;
        client->response.read_state = READ_END;
    } else {
        istream_init(&client->response.body, &ajp_response_body, client->pool);
        body = istream_struct_cast(&client->response.body);
        client->response.read_state = READ_BODY;
        client->response.chunk_length = 0;
        client->response.junk_length = 0;
    }

    client->response.in_handler = true;
    http_response_handler_invoke_response(&client->request.handler, status,
                                          headers, body);
    client->response.in_handler = false;

    return client->fd >= 0;
}

static bool
ajp_consume_packet(struct ajp_client *client, ajp_code_t code,
                   const char *data, size_t length)
{
    const struct ajp_get_body_chunk *chunk;

    switch (code) {
    case AJP_CODE_FORWARD_REQUEST:
    case AJP_CODE_SHUTDOWN:
    case AJP_CODE_CPING:
        daemon_log(1, "unexpected request packet from AJP server\n");
        ajp_connection_close(client);
        return false;

    case AJP_CODE_SEND_BODY_CHUNK:
        assert(0); /* already handled in ajp_consume_input() */
        return false;

    case AJP_CODE_SEND_HEADERS:
        return ajp_consume_send_headers(client, data, length);

    case AJP_CODE_END_RESPONSE:
        if (client->response.read_state == READ_BODY) {
            client->response.read_state = READ_END;
            ajp_client_release(client, true);
            istream_deinit_eof(&client->response.body);
        } else
            ajp_client_release(client, true);

        return false;

    case AJP_CODE_GET_BODY_CHUNK:
        chunk = (const struct ajp_get_body_chunk *)(data - 1);

        if (length < sizeof(*chunk) - 1) {
            daemon_log(1, "malformed AJP GET_BODY_CHUNK packet\n");
            ajp_connection_close(client);
            return false;
        }

        if (client->request.istream == NULL ||
            client->request.ajp_body == NULL) {
            /* we always send empty_body_chunk to the AJP server, so
               we can safely ignore all other AJP_CODE_GET_BODY_CHUNK
               requests here */
            return true;
        }

        istream_ajp_body_request(client->request.ajp_body,
                                 ntohs(chunk->length));
        event2_or(&client->event, EV_WRITE);
        return true;

    case AJP_CODE_CPONG_REPLY:
        /* XXX */
        break;
    }

    daemon_log(1, "unknown packet from AJP server\n");
    ajp_connection_close(client);
    return false;
}

static bool
ajp_consume_body_chunk(struct ajp_client *client)
{
    const char *data;
    size_t length, nbytes;

    assert(client->response.read_state == READ_BODY);
    assert(client->response.chunk_length > 0);

    data = fifo_buffer_read(client->response.input, &length);
    if (data == NULL)
        return false;

    if (length > client->response.chunk_length)
        length = client->response.chunk_length;

    nbytes = istream_invoke_data(&client->response.body, data, length);
    if (nbytes == 0)
        return false;

    fifo_buffer_consume(client->response.input, nbytes);
    client->response.chunk_length -= nbytes;
    return client->response.chunk_length == 0;
}

static bool
ajp_consume_body_junk(struct ajp_client *client)
{
    const char *data;
    size_t length;

    assert(client->response.read_state == READ_BODY);
    assert(client->response.chunk_length == 0);
    assert(client->response.junk_length > 0);

    data = fifo_buffer_read(client->response.input, &length);
    if (data == NULL)
        return false;

    if (length > client->response.junk_length)
        length = client->response.junk_length;

    fifo_buffer_consume(client->response.input, length);
    client->response.junk_length -= length;
    return client->response.junk_length == 0;
}

static void
ajp_consume_input(struct ajp_client *client)
{
    const char *data;
    size_t length, header_length;
    const struct ajp_header *header;
    ajp_code_t code;
    bool bret;

    assert(client != NULL);
    assert(client->response.read_state == READ_BEGIN ||
           client->response.read_state == READ_BODY);

    while (true) {
        if (client->response.read_state == READ_BODY) {
            /* there is data left from the previous body chunk */
            if (client->response.chunk_length > 0 &&
                !ajp_consume_body_chunk(client))
                return;

            if (client->response.junk_length > 0 &&
                !ajp_consume_body_junk(client))
                return;
        }

        data = fifo_buffer_read(client->response.input, &length);
        if (data == NULL || length < sizeof(*header))
            /* we need a full header */
            return;

        header = (const struct ajp_header*)data;
        header_length = ntohs(header->length);

        if (header->a != 'A' || header->b != 'B' || header_length == 0) {
            daemon_log(1, "malformed AJP response packet\n");
            ajp_connection_close(client);
            return;
        }

        if (length < sizeof(*header) + 1)
            /* we need the prefix code */
            return;

        code = data[sizeof(*header)];

        if (code == AJP_CODE_SEND_BODY_CHUNK) {
            const struct ajp_send_body_chunk *chunk =
                (const struct ajp_send_body_chunk *)(header + 1);

            if (client->response.read_state != READ_BODY) {
                daemon_log(1, "unexpected SEND_BODY_CHUNK packet from AJP server\n");
                ajp_connection_close(client);
                return;
            }

            if (length < sizeof(*header) + sizeof(*chunk))
                /* we need the chunk length */
                return;

            client->response.chunk_length = ntohs(chunk->length);
            if (sizeof(*chunk) + client->response.chunk_length > header_length) {
                daemon_log(1, "malformed AJP SEND_BODY_CHUNK packet\n");
                ajp_connection_close(client);
                return;
            }

            client->response.junk_length = header_length - sizeof(*chunk) - client->response.chunk_length;

            fifo_buffer_consume(client->response.input, sizeof(*header) + sizeof(*chunk));
            if (client->response.chunk_length > 0 &&
                !ajp_consume_body_chunk(client))
                return;

            if (client->response.junk_length > 0 &&
                !ajp_consume_body_junk(client))
                return;

            continue;
        }

        if (length < sizeof(*header) + header_length) {
            /* the packet is not complete yet */

            if (fifo_buffer_full(client->response.input)) {
                daemon_log(1, "too large packet from AJP server\n");
                ajp_connection_close(client);
            }

            return;
        }

        bret = ajp_consume_packet(client, code,
                                  data + sizeof(*header) + 1, header_length - 1);
        if (!bret)
            return;

        fifo_buffer_consume(client->response.input,
                            sizeof(*header) + header_length);
    }
}
 
static void
ajp_try_read(struct ajp_client *client)
{
    ssize_t nbytes;

    nbytes = read_to_buffer(client->fd, client->response.input,
                            INT_MAX);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "AJP server closed the connection\n");
        ajp_connection_close(client);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&client->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on AJP connection: %s\n", strerror(errno));
        ajp_connection_close(client);
        return;
    }

    ajp_consume_input(client);

    if (ajp_connection_valid(client) &&
        !fifo_buffer_full(client->response.input))
        event2_setbit(&client->event, EV_READ,
                      !fifo_buffer_full(client->response.input));
}

static void
ajp_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct ajp_client *client = ctx;

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        ajp_connection_close(client);
        return;
    }

    pool_ref(client->pool);

    event2_reset(&client->event);
    event2_lock(&client->event);
    event2_or(&client->event, EV_READ);

    if (ajp_connection_valid(client) && (event & EV_WRITE) != 0) {
        socket_set_cork(client->fd, true);
        istream_read(client->request.istream);
        if (client->fd >= 0)
            socket_set_cork(client->fd, false);
    }

    if (ajp_connection_valid(client) && (event & EV_READ) != 0)
        ajp_try_read(client);

    event2_unlock(&client->event);

    pool_unref(client->pool);
    pool_commit();
}


/*
 * istream handler for the request
 *
 */

static size_t
ajp_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct ajp_client *client = ctx;

    assert(client->fd >= 0);
    assert(client->request.istream != NULL);

    return ajp_write(client, data, length);
}

static void
ajp_request_stream_eof(void *ctx)
{
    struct ajp_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    event2_nand(&client->event, EV_WRITE);
}

static void
ajp_request_stream_abort(void *ctx)
{
    struct ajp_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    ajp_connection_close(client);
}

static const struct istream_handler ajp_request_stream_handler = {
    .data = ajp_request_stream_data,
    .eof = ajp_request_stream_eof,
    .abort = ajp_request_stream_abort,
};


/*
 * async operation
 *
 */

static struct ajp_client *
async_to_ajp_connection(struct async_operation *ao)
{
    return (struct ajp_client*)(((char*)ao) - offsetof(struct ajp_client, request.async));
}

static void
ajp_client_request_abort(struct async_operation *ao)
{
    struct ajp_client *client
        = async_to_ajp_connection(ao);
    
    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == READ_BEGIN);

    /* by setting the state to READ_END, we bar
       ajp_client_request_close() from invoking the "abort"
       callback */
    client->response.read_state = READ_END;

    ajp_connection_close(client);
}

static const struct async_operation_class ajp_client_request_async_operation = {
    .abort = ajp_client_request_abort,
};


/*
 * constructor
 *
 */

void
ajp_client_request(pool_t pool, int fd,
                   const struct lease *lease, void *lease_ctx,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   struct strmap *headers,
                   istream_t body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    struct ajp_client *client;
    struct growing_buffer *gb;
    struct ajp_header *header;
    ajp_method_t ajp_method;
    struct {
        uint8_t prefix_code, method;
    } prefix_and_method;
    struct growing_buffer *headers_buffer = NULL;
    unsigned num_headers;
    istream_t request;
    size_t requested;

    assert(protocol != NULL);
    assert(remote_addr != NULL);
    assert(remote_host != NULL);
    assert(server_name != NULL);
    assert(http_method_is_valid(method));

    (void)headers; /* XXX */

    pool_ref(pool);
    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->fd = fd;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);
    event2_init(&client->event, fd, ajp_event_callback, client, NULL);

    gb = growing_buffer_new(pool, 256);

    header = growing_buffer_write(gb, sizeof(*header));
    header->a = 0x12;
    header->b = 0x34;

    ajp_method = to_ajp_method(method);
    if (ajp_method == AJP_METHOD_NULL) {
        /* invalid or unknown method */
        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    prefix_and_method.prefix_code = AJP_PREFIX_FORWARD_REQUEST;
    prefix_and_method.method = (uint8_t)ajp_method;

    growing_buffer_write_buffer(gb, &prefix_and_method, sizeof(prefix_and_method));

    serialize_ajp_string(gb, protocol);
    serialize_ajp_string(gb, uri);
    serialize_ajp_string(gb, remote_addr);
    serialize_ajp_string(gb, remote_host);
    serialize_ajp_string(gb, server_name);
    serialize_ajp_integer(gb, server_port);
    serialize_ajp_bool(gb, is_ssl);

    if (headers != NULL) {
        /* serialize the request headers - note that
           serialize_ajp_headers() ignores the Content-Length header,
           we will append it later */
        headers_buffer = growing_buffer_new(pool, 2048);
        num_headers = serialize_ajp_headers(headers_buffer, headers);
    } else
        num_headers = 0;

    if (body != NULL)
        /* if there's a request body, we'll append the Content-Length
           header */
        ++num_headers;

    serialize_ajp_integer(gb, num_headers);
    if (headers != NULL)
        growing_buffer_cat(gb, headers_buffer);

    if (body != NULL) {
        off_t available;
        char buffer[32];

        available = istream_available(body, false);
        if (available == -1) {
            /* AJPv13 does not support chunked request bodies */
            istream_close(body);
            http_response_handler_direct_abort(handler, handler_ctx);
            return;
        }

        format_uint64(buffer, (uint64_t)available);
        serialize_ajp_integer(gb, AJP_HEADER_CONTENT_LENGTH);
        serialize_ajp_string(gb, buffer);

        if (available == 0)
            istream_free(&body);
        else
            requested = 1024;
    }

    growing_buffer_write_buffer(gb, "\xff", 1);
    
    /* XXX is this correct? */

    header->length = htons(growing_buffer_size(gb) - sizeof(*header));

    if (body == NULL)
        growing_buffer_write_buffer(gb, &empty_body_chunk,
                                    sizeof(empty_body_chunk));

    request = growing_buffer_istream(gb);
    if (body != NULL) {
        client->request.ajp_body = istream_ajp_body_new(pool, body);
        istream_ajp_body_request(client->request.ajp_body, requested);
        request = istream_cat_new(pool, request, client->request.ajp_body,
                                  istream_memory_new(pool, &empty_body_chunk,
                                                     sizeof(empty_body_chunk)),
                                  NULL);
    } else {
        client->request.ajp_body = NULL;
    }

    istream_assign_handler(&client->request.istream, request,
                           &ajp_request_stream_handler, client,
                           0);

    http_response_handler_set(&client->request.handler, handler, handler_ctx);

    async_init(&client->request.async,
               &ajp_client_request_async_operation);
    async_ref_set(async_ref, &client->request.async);

    /* XXX append request body */

    client->response.read_state = READ_BEGIN;
    client->response.in_handler = false;
    client->response.input = fifo_buffer_new(client->pool, 8192);
    client->response.headers = NULL;

    pool_ref(client->pool);

    event2_lock(&client->event);
    event2_set(&client->event, EV_READ);

    socket_set_cork(client->fd, true);
    istream_read(client->request.istream);
    if (client->fd >= 0)
        socket_set_cork(client->fd, false);

    event2_unlock(&client->event);

    pool_unref(client->pool);
}
