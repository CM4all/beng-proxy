/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp-client.h"
#include "http-client.h"
#include "http-response.h"
#include "async.h"
#include "fifo-buffer.h"
#include "growing-buffer.h"
#include "event2.h"
#include "format.h"
#include "buffered-io.h"
#include "istream-internal.h"
#include "ajp-protocol.h"
#include "ajp-write.h"

#include <daemon/log.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

struct ajp_connection {
    pool_t pool;

    /* I/O */
    int fd;
    struct event2 event;

    /* handler */
    const struct http_client_connection_handler *handler;
    void *handler_ctx;

    /* request */
    struct {
        pool_t pool;
        istream_t istream;

        struct http_response_handler_ref handler;
        struct async_operation async;
    } request;

    /* response */
    struct {
        enum {
            READ_BEGIN,
            READ_BODY,
            READ_END,
            READ_ABORTED,
        } read_state;
        fifo_buffer_t input;
        http_status_t status;
        struct strmap *headers;
        struct istream body;
        size_t chunk_length, junk_length;
    } response;
};

static inline bool
ajp_connection_valid(struct ajp_connection *connection)
{
    return connection->fd >= 0;
}

static void
ajp_consume_input(struct ajp_connection *connection);

static void
ajp_try_read(struct ajp_connection *connection);

void
ajp_connection_close(struct ajp_connection *connection)
{
    if (connection->fd >= 0) {
        event2_set(&connection->event, 0);
        close(connection->fd);
        connection->fd = -1;
    }
}


/*
 * response body stream
 *
 */

static inline struct ajp_connection *
istream_to_ajp(istream_t istream)
{
    return (struct ajp_connection *)(((char*)istream) - offsetof(struct ajp_connection, response.body));
}

static void
istream_ajp_read(istream_t istream)
{
    struct ajp_connection *connection = istream_to_ajp(istream);

    assert(connection->request.pool != NULL);
    assert(connection->response.read_state == READ_BODY);

    if (fifo_buffer_full(connection->response.input))
        ajp_consume_input(connection);
    else
        ajp_try_read(connection);
}

static void
istream_ajp_close(istream_t istream)
{
    struct ajp_connection *connection = istream_to_ajp(istream);

    assert(connection->request.pool != NULL);
    assert(connection->response.read_state == READ_BODY);

    ajp_connection_close(connection);
}

static const struct istream ajp_response_body = {
    /* XXX .available */
    .read = istream_ajp_read,
    .close = istream_ajp_close,
};


/*
 * response parser
 *
 */

static bool
ajp_consume_send_headers(struct ajp_connection *connection,
                         const char *data, size_t length)
{
    http_status_t status;
    size_t msg_length;
    unsigned num_headers;
    istream_t body;

    if (connection->response.read_state != READ_BEGIN) {
        daemon_log(1, "unexpected SEND_HEADERS packet from AJP server\n");
        ajp_connection_close(connection);
        return false;
    }

    if (2 + 3 + 2 > length) {
        daemon_log(1, "malformed SEND_HEADERS packet from AJP server\n");
        ajp_connection_close(connection);
        return false;
    }

    status = ntohs(*(const uint16_t*)data);
    msg_length = ntohs(*(const uint16_t*)(data + 2));
    if (2 + 3 + msg_length + 2 > length) {
        daemon_log(1, "malformed SEND_HEADERS packet from AJP server\n");
        ajp_connection_close(connection);
        return false;
    }

    data += 2 + 3 + msg_length;
    length -= 2 + 3 + msg_length;

    num_headers = ntohs(*(const uint16_t*)data);
    data += 2;
    length -= 2;

    /* XXX parse headers */

    if (http_status_is_empty(status)) {
        body = NULL;
        connection->response.read_state = READ_END;
    } else {
        istream_init(&connection->response.body, &ajp_response_body, connection->request.pool);
        body = istream_struct_cast(&connection->response.body);
        connection->response.read_state = READ_BODY;
        connection->response.chunk_length = 0;
        connection->response.junk_length = 0;
    }

    http_response_handler_invoke_response(&connection->request.handler, status,
                                          NULL, body);
    return true;
}

static bool
ajp_consume_packet(struct ajp_connection *connection, ajp_code_t code,
                   const char *data, size_t length)
{
    (void)data; (void)length; /* XXX */

    switch (code) {
    case AJP_CODE_FORWARD_REQUEST:
    case AJP_CODE_SHUTDOWN:
    case AJP_CODE_CPING:
        daemon_log(1, "unexpected request packet from AJP server\n");
        ajp_connection_close(connection);
        return false;

    case AJP_CODE_SEND_BODY_CHUNK:
        assert(0); /* already handled in ajp_consume_input() */
        return false;

    case AJP_CODE_SEND_HEADERS:
        return ajp_consume_send_headers(connection, data, length);

    case AJP_CODE_END_RESPONSE:
        if (connection->response.read_state == READ_BODY) {
            connection->response.read_state = READ_END;
            istream_deinit_eof(&connection->response.body);
        }

        return true;

    case AJP_CODE_GET_BODY_CHUNK:
        /* XXX */
        break;

    case AJP_CODE_CPONG_REPLY:
        /* XXX */
        break;
    }

    daemon_log(1, "unknown packet from AJP server\n");
    ajp_connection_close(connection);
    return false;
}

static bool
ajp_consume_body_chunk(struct ajp_connection *connection)
{
    const char *data;
    size_t length, nbytes;

    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.chunk_length > 0);

    data = fifo_buffer_read(connection->response.input, &length);
    if (data == NULL)
        return false;

    if (length > connection->response.chunk_length)
        length = connection->response.chunk_length;

    nbytes = istream_invoke_data(&connection->response.body, data, length);
    if (nbytes == 0)
        return false;

    fifo_buffer_consume(connection->response.input, nbytes);
    connection->response.chunk_length -= nbytes;
    return connection->response.chunk_length == 0;
}

static bool
ajp_consume_body_junk(struct ajp_connection *connection)
{
    const char *data;
    size_t length;

    assert(connection->response.read_state == READ_BODY);
    assert(connection->response.chunk_length == 0);
    assert(connection->response.junk_length > 0);

    data = fifo_buffer_read(connection->response.input, &length);
    if (data == NULL)
        return false;

    if (length > connection->response.junk_length)
        length = connection->response.junk_length;

    fifo_buffer_consume(connection->response.input, length);
    connection->response.junk_length -= length;
    return connection->response.junk_length == 0;
}

static void
ajp_consume_input(struct ajp_connection *connection)
{
    const char *data;
    size_t length, header_length;
    const struct ajp_header *header;
    ajp_code_t code;
    bool bret;

    assert(connection != NULL);
    assert(connection->request.pool != NULL);
    assert(connection->response.read_state == READ_BEGIN ||
           connection->response.read_state == READ_BODY);

    while (true) {
        data = fifo_buffer_read(connection->response.input, &length);
        if (data == NULL)
            return;

        if (connection->response.read_state == READ_BODY) {
            /* there is data left from the previous body chunk */
            if (connection->response.chunk_length > 0 &&
                !ajp_consume_body_chunk(connection))
                return;

            if (connection->response.junk_length > 0 &&
                !ajp_consume_body_junk(connection))
                return;
        }

        if (length < sizeof(*header))
            /* we need a full header */
            return;

        header = (const struct ajp_header*)data;
        header_length = ntohs(header->length);

        if (header->a != 'A' || header->b != 'B' || header_length == 0) {
            daemon_log(1, "malformed AJP response packet\n");
            ajp_connection_close(connection);
            return;
        }

        if (length < sizeof(*header) + 1)
            /* we need the prefix code */
            return;

        code = data[sizeof(*header)];

        if (code == AJP_CODE_SEND_BODY_CHUNK) {
            const struct ajp_send_body_chunk *chunk =
                (const struct ajp_send_body_chunk *)(header + 1);

            if (connection->response.read_state != READ_BODY) {
                daemon_log(1, "unexpected SEND_BODY_CHUNK packet from AJP server\n");
                ajp_connection_close(connection);
                return;
            }

            if (length < sizeof(*header) + sizeof(*chunk))
                /* we need the chunk length */
                return;

            connection->response.chunk_length = ntohs(chunk->length);
            if (sizeof(*chunk) + connection->response.chunk_length > header_length) {
                daemon_log(1, "malformed AJP SEND_BODY_CHUNK packet\n");
                ajp_connection_close(connection);
                return;
            }

            connection->response.junk_length = header_length - sizeof(*chunk) - connection->response.chunk_length;

            fifo_buffer_consume(connection->response.input, sizeof(*header) + sizeof(*chunk));
            if (connection->response.chunk_length > 0 &&
                !ajp_consume_body_chunk(connection))
                return;

            if (connection->response.junk_length > 0 &&
                !ajp_consume_body_junk(connection))
                return;

            continue;
        }

        if (length < sizeof(*header) + header_length) {
            /* the packet is not complete yet */

            if (fifo_buffer_full(connection->response.input)) {
                daemon_log(1, "too large packet from AJP server\n");
                ajp_connection_close(connection);
            }

            return;
        }

        bret = ajp_consume_packet(connection, code,
                                  data + sizeof(*header) + 1, header_length - 1);
        if (!bret)
            return;

        fifo_buffer_consume(connection->response.input,
                            sizeof(*header) + header_length);
    }
}
 
static void
ajp_try_read(struct ajp_connection *connection)
{
    ssize_t nbytes;

    if (connection->request.pool == NULL) {
        char buffer;

        nbytes = read(connection->fd, &buffer, sizeof(buffer));
    } else {
        nbytes = read_to_buffer(connection->fd, connection->response.input,
                                INT_MAX);
        assert(nbytes != -2);
    }

    if (nbytes == 0) {
        if (connection->request.pool != NULL)
            daemon_log(1, "AJP server closed the connection\n");

        ajp_connection_close(connection);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&connection->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on AJP connection: %s\n", strerror(errno));
        ajp_connection_close(connection);
        return;
    }

    if (connection->request.pool == NULL) {
        daemon_log(2, "excess data received on idle AJP client socket\n");
        ajp_connection_close(connection);
        return;
    }

    ajp_consume_input(connection);

    if (ajp_connection_valid(connection) &&
        (connection->request.pool == NULL || !fifo_buffer_full(connection->response.input)))
        event2_setbit(&connection->event, EV_READ,
                      connection->request.pool == NULL ||
                      !fifo_buffer_full(connection->response.input));
}

static void
ajp_event_callback(int fd __attr_unused, short event, void *ctx)
{
    struct ajp_connection *connection = ctx;

    pool_ref(connection->pool);

    event2_reset(&connection->event);
    event2_lock(&connection->event);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        ajp_connection_close(connection);
    }

    if (ajp_connection_valid(connection) && (event & EV_WRITE) != 0)
        istream_read(connection->request.istream);

    if (ajp_connection_valid(connection) && (event & EV_READ) != 0)
        ajp_try_read(connection);

    event2_unlock(&connection->event);

    pool_unref(connection->pool);
    pool_commit();
}

struct ajp_connection *
ajp_new(pool_t pool, int fd,
        const struct http_client_connection_handler *handler, void *ctx)
{
    struct ajp_connection *connection = p_malloc(pool, sizeof(*connection));

    connection->pool = pool;
    connection->fd = fd;
    connection->handler = handler;
    connection->handler_ctx = ctx;
    event2_init(&connection->event, fd, ajp_event_callback, connection, NULL);

    return connection;
}


/*
 * istream handler for the request
 *
 */

static size_t
ajp_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct ajp_connection *connection = ctx;
    ssize_t nbytes;

    assert(connection->fd >= 0);
    assert(connection->request.pool != NULL);
    assert(connection->request.istream != NULL);

    nbytes = write(connection->fd, data, length);
    if (likely(nbytes >= 0)) {
        event2_or(&connection->event, EV_WRITE);
        return (size_t)nbytes;
    }

    if (likely(errno == EAGAIN)) {
        event2_or(&connection->event, EV_WRITE);
        return 0;
    }

    daemon_log(1, "write error on AJP client connection: %s\n",
               strerror(errno));
    ajp_connection_close(connection);
    return 0;
}

static void
ajp_request_stream_eof(void *ctx)
{
    struct ajp_connection *connection = ctx;

    assert(connection->request.pool != NULL);
    assert(connection->request.istream != NULL);

    connection->request.istream = NULL;

    connection->response.read_state = READ_BEGIN;
    connection->response.input = fifo_buffer_new(connection->request.pool, 8192);
    connection->response.headers = NULL;

    event2_set(&connection->event, EV_READ);
}

static void
ajp_request_stream_abort(void *ctx)
{
    struct ajp_connection *connection = ctx;

    assert(connection->request.pool != NULL);
    assert(connection->request.istream != NULL);

    ajp_connection_close(connection);
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

static struct ajp_connection *
async_to_ajp_connection(struct async_operation *ao)
{
    return (struct ajp_connection*)(((char*)ao) - offsetof(struct ajp_connection, request.async));
}

static void
ajp_client_request_abort(struct async_operation *ao)
{
    struct ajp_connection *connection
        = async_to_ajp_connection(ao);
    
    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(connection->response.read_state == READ_BEGIN);

    /* by setting the state to READ_ABORTED, we bar
       ajp_client_request_close() from invoking the "abort"
       callback */
    connection->response.read_state = READ_ABORTED;
    pool_unref(connection->request.pool);

    ajp_connection_close(connection);
}

static struct async_operation_class ajp_client_request_async_operation = {
    .abort = ajp_client_request_abort,
};


void
ajp_request(struct ajp_connection *connection, pool_t pool,
            http_method_t method, const char *uri,
            struct strmap *headers,
            istream_t body,
            const struct http_response_handler *handler,
            void *handler_ctx,
            struct async_operation_ref *async_ref)
{
    struct growing_buffer *gb;
    struct ajp_header *header;
    ajp_method_t ajp_method;
    struct {
        uint8_t prefix_code, method;
    } prefix_and_method;

    (void)headers; /* XXX */

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

    gb_write_ajp_string(gb, "http");
    gb_write_ajp_string(gb, uri);
    gb_write_ajp_string(gb, "127.0.0.1"); /* XXX remote_addr */
    gb_write_ajp_string(gb, "localhost"); /* XXX remote_host */
    gb_write_ajp_string(gb, "localhost"); /* XXX server_name */
    gb_write_ajp_integer(gb, 80); /* XXX server_port */
    gb_write_ajp_bool(gb, false); /* is_ssl */
    gb_write_ajp_integer(gb, body == NULL ? 0 : 1); /* XXX num_headers */

    if (body != NULL) {
        char buffer[32];
        off_t content_length = istream_available(body, false);
        if (content_length == -1) {
            /* AJPv13 does not support chunked request bodies */
            http_response_handler_direct_abort(handler, handler_ctx);
            return;
        }

        format_uint64(buffer, (uint64_t)content_length);
        gb_write_ajp_integer(gb, AJP_HEADER_CONTENT_LENGTH);
        gb_write_ajp_string(gb, buffer);
    }

    growing_buffer_write_buffer(gb, "\xff", 1);
    
    connection->request.pool = pool;
    connection->request.istream = growing_buffer_istream(gb);

    /* XXX is this correct? */
    header->length = htons(istream_available(connection->request.istream, false) - sizeof(*header));

    http_response_handler_set(&connection->request.handler, handler, handler_ctx);

    async_init(&connection->request.async,
               &ajp_client_request_async_operation);
    async_ref_set(async_ref, &connection->request.async);

    /* XXX append request body */

    connection->response.read_state = READ_BEGIN;


    istream_handler_set(connection->request.istream,
                        &ajp_request_stream_handler, connection,
                        0);

    pool_ref(connection->pool);

    event2_lock(&connection->event);

    istream_read(connection->request.istream);

    event2_unlock(&connection->event);

    pool_unref(connection->pool);
}
