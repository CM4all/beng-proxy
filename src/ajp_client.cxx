/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ajp_client.hxx"
#include "ajp_headers.hxx"
#include "ajp_serialize.hxx"
#include "buffered_socket.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "growing_buffer.hxx"
#include "pevent.hxx"
#include "format.h"
#include "istream-internal.h"
#include "istream_cat.hxx"
#include "istream_gb.hxx"
#include "istream_ajp_body.hxx"
#include "ajp-protocol.h"
#include "serialize.hxx"
#include "strref.h"
#include "please.hxx"
#include "uri_verify.hxx"
#include "direct.h"
#include "fd-util.h"
#include "strmap.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"
#include "pool.hxx"

#include <daemon/log.h>
#include <socket/util.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <limits.h>

struct ajp_client {
    struct pool *pool;

    /* I/O */
    BufferedSocket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        struct istream *istream;

        /** an istream_ajp_body */
        struct istream *ajp_body;

        /**
         * This flag is set when the request istream has submitted
         * data.  It is used to check whether the request istream is
         * unavailable, to unschedule the socket write event.
         */
        bool got_data;

        struct http_response_handler_ref handler;
    } request;

    struct async_operation request_async;

    /* response */
    struct Response {
        enum {
            READ_BEGIN,

            /**
             * The #AJP_CODE_SEND_HEADERS indicates that there is no
             * response body.  Waiting for the #AJP_CODE_END_RESPONSE
             * packet, and then we'll forward the response to the
             * #http_response_handler.
             */
            READ_NO_BODY,

            READ_BODY,
            READ_END,
        } read_state;

        /**
         * This flag is true in HEAD requests.  HEAD responses may
         * contain a Content-Length header, but no response body will
         * follow (RFC 2616 4.3).
         */
        bool no_body;

        /**
         * This flag is true if ajp_consume_send_headers() is
         * currently calling the HTTP response handler.  During this
         * period, istream_ajp_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        http_status_t status;

        /**
         * Only used when read_state==READ_NO_BODY.
         */
        struct strmap *headers;

        size_t chunk_length, junk_length;

        /**
         * The remaining response body, -1 if unknown.
         */
        off_t remaining;
    } response;

    struct istream response_body;
};

static const struct timeval ajp_client_timeout = {
    .tv_sec = 30,
    .tv_usec = 0,
};

static const struct ajp_header empty_body_chunk = {
    .a = 0x12, .b = 0x34,
};

static void
ajp_client_schedule_write(struct ajp_client *client)
{
    client->socket.ScheduleWrite();
}

/**
 * Release the AJP connection socket.
 */
static void
ajp_client_release_socket(struct ajp_client *client, bool reuse)
{
    assert(client != nullptr);
    assert(client->socket.IsConnected());
    assert(client->response.read_state == ajp_client::Response::READ_BODY ||
           client->response.read_state == ajp_client::Response::READ_END);

    client->socket.Abandon();
    p_lease_release(client->lease_ref, reuse, *client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, the request body and the pool reference.
 */
static void
ajp_client_release(struct ajp_client *client, bool reuse)
{
    assert(client != nullptr);
    assert(client->socket.IsValid());
    assert(client->response.read_state == ajp_client::Response::READ_END);

    if (client->socket.IsConnected())
        ajp_client_release_socket(client, reuse);

    client->socket.Destroy();

    if (client->request.istream != nullptr)
        istream_free_handler(&client->request.istream);

    pool_unref(client->pool);
}

static void
ajp_client_abort_response_headers(struct ajp_client *client, GError *error)
{
    assert(client != nullptr);
    assert(client->response.read_state == ajp_client::Response::READ_BEGIN ||
           client->response.read_state == ajp_client::Response::READ_NO_BODY);

    const ScopePoolRef ref(*client->pool TRACE_ARGS);

    client->response.read_state = ajp_client::Response::READ_END;
    client->request_async.Finished();
    client->request.handler.InvokeAbort(error);

    ajp_client_release(client, false);
}

/**
 * Abort the response body.
 */
static void
ajp_client_abort_response_body(struct ajp_client *client, GError *error)
{
    assert(client != nullptr);
    assert(client->response.read_state == ajp_client::Response::READ_BODY);

    const ScopePoolRef ref(*client->pool TRACE_ARGS);

    client->response.read_state = ajp_client::Response::READ_END;
    istream_deinit_abort(&client->response_body, error);

    ajp_client_release(client, false);
}

static void
ajp_client_abort_response(struct ajp_client *client, GError *error)
{
    assert(client != nullptr);
    assert(client->response.read_state != ajp_client::Response::READ_END);

    switch (client->response.read_state) {
    case ajp_client::Response::READ_BEGIN:
    case ajp_client::Response::READ_NO_BODY:
        ajp_client_abort_response_headers(client, error);
        break;

    case ajp_client::Response::READ_BODY:
        ajp_client_abort_response_body(client, error);
        break;

    case ajp_client::Response::READ_END:
        assert(false);
        break;
    }
}


/*
 * response body stream
 *
 */

static inline struct ajp_client *
istream_to_ajp(struct istream *istream)
{
    return &ContainerCast2(*istream, &ajp_client::response_body);
}

static off_t
istream_ajp_available(struct istream *istream, bool partial)
{
    struct ajp_client *client = istream_to_ajp(istream);

    assert(client->response.read_state == ajp_client::Response::READ_BODY);

    if (client->response.remaining >= 0)
        /* the Content-Length was announced by the AJP server */
        return client->response.remaining;

    if (partial)
        /* we only know how much is left in the current chunk */
        return client->response.chunk_length;

    /* no clue */
    return -1;
}

static void
istream_ajp_read(struct istream *istream)
{
    struct ajp_client *client = istream_to_ajp(istream);

    assert(client->response.read_state == ajp_client::Response::READ_BODY);

    if (client->response.in_handler)
        return;

    client->socket.Read(true);
}

static void
istream_ajp_close(struct istream *istream)
{
    struct ajp_client *client = istream_to_ajp(istream);

    assert(client->response.read_state == ajp_client::Response::READ_BODY);

    client->response.read_state = ajp_client::Response::READ_END;

    ajp_client_release(client, false);
    istream_deinit(&client->response_body);
}

static const struct istream_class ajp_response_body = {
    .available = istream_ajp_available,
    .read = istream_ajp_read,
    .close = istream_ajp_close,
};


/*
 * response parser
 *
 */

/**
 * @return false if the #ajp_client has been closed
 */
static bool
ajp_consume_send_headers(struct ajp_client *client,
                         const uint8_t *data, size_t length)
{
    unsigned num_headers;
    struct istream *body;
    struct strmap *headers;

    if (client->response.read_state != ajp_client::Response::READ_BEGIN) {
        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "unexpected SEND_HEADERS packet from AJP server");
        ajp_client_abort_response_body(client, error);
        return false;
    }

    ConstBuffer<void> packet(data, length);
    http_status_t status = (http_status_t)deserialize_uint16(packet);
    deserialize_ajp_string(packet);
    num_headers = deserialize_uint16(packet);

    if (num_headers > 0) {
        headers = strmap_new(client->pool);
        deserialize_ajp_response_headers(client->pool, headers,
                                         packet, num_headers);
    } else
        headers = nullptr;

    if (packet.IsNull()) {
        GError *error =
            g_error_new_literal(ajp_client_quark(), 0,
                                "malformed SEND_HEADERS packet from AJP server");
        ajp_client_abort_response_headers(client, error);
        return false;
    }

    if (!http_status_is_valid(status)) {
        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "invalid status %u from AJP server", status);
        ajp_client_abort_response_headers(client, error);
        return false;
    }

    if (client->response.no_body || http_status_is_empty(status)) {
        client->response.read_state = ajp_client::Response::READ_NO_BODY;
        client->response.status = status;
        client->response.headers = headers;
        client->response.chunk_length = 0;
        client->response.junk_length = 0;
        return true;
    }

    const char *content_length = strmap_remove_checked(headers, "content-length");
    if (content_length != nullptr) {
        char *endptr;
        client->response.remaining = strtoul(content_length, &endptr, 10);
        if (endptr == content_length || *endptr != 0) {
            GError *error =
                g_error_new_literal(ajp_client_quark(), 0,
                                    "Malformed Content-Length from AJP server");
            ajp_client_abort_response_headers(client, error);
            return false;
        }
    } else
        client->response.remaining = -1;

    istream_init(&client->response_body, &ajp_response_body, client->pool);
    body = istream_struct_cast(&client->response_body);
    client->response.read_state = ajp_client::Response::READ_BODY;
    client->response.chunk_length = 0;
    client->response.junk_length = 0;

    client->request_async.Finished();

    client->response.in_handler = true;
    client->request.handler.InvokeResponse(status, headers, body);
    client->response.in_handler = false;

    return client->socket.IsValid();
}

/**
 * @return false if the #ajp_client has been closed
 */
static bool
ajp_consume_packet(struct ajp_client *client, enum ajp_code code,
                   const uint8_t *data, size_t length)
{
    const struct ajp_get_body_chunk *chunk;
    GError *error;

    switch (code) {
    case AJP_CODE_FORWARD_REQUEST:
    case AJP_CODE_SHUTDOWN:
    case AJP_CODE_CPING:
        error = g_error_new_literal(ajp_client_quark(), 0,
                                    "unexpected request packet from AJP server");
        ajp_client_abort_response(client, error);
        return false;

    case AJP_CODE_SEND_BODY_CHUNK:
        assert(0); /* already handled in ajp_client_feed() */
        return false;

    case AJP_CODE_SEND_HEADERS:
        return ajp_consume_send_headers(client, data, length);

    case AJP_CODE_END_RESPONSE:
        if (client->response.read_state == ajp_client::Response::READ_BODY) {
            if (client->response.remaining > 0) {
                error = g_error_new_literal(ajp_client_quark(), 0,
                                            "premature end of response AJP server");
                ajp_client_abort_response(client, error);
                return false;
            }

            client->response.read_state = ajp_client::Response::READ_END;
            ajp_client_release(client, true);
            istream_deinit_eof(&client->response_body);
        } else if (client->response.read_state == ajp_client::Response::READ_NO_BODY) {
            client->response.read_state = ajp_client::Response::READ_END;
            ajp_client_release(client, client->socket.IsEmpty());

            client->request.handler.InvokeResponse(client->response.status,
                                                   client->response.headers,
                                                   nullptr);
        } else
            ajp_client_release(client, true);

        return false;

    case AJP_CODE_GET_BODY_CHUNK:
        chunk = (const struct ajp_get_body_chunk *)(data - 1);

        if (length < sizeof(*chunk) - 1) {
            error = g_error_new_literal(ajp_client_quark(), 0,
                                        "malformed AJP GET_BODY_CHUNK packet");
            ajp_client_abort_response(client, error);
            return false;
        }

        if (client->request.istream == nullptr ||
            client->request.ajp_body == nullptr) {
            /* we always send empty_body_chunk to the AJP server, so
               we can safely ignore all other AJP_CODE_GET_BODY_CHUNK
               requests here */
            return true;
        }

        istream_ajp_body_request(client->request.ajp_body,
                                 FromBE16(chunk->length));
        ajp_client_schedule_write(client);
        return true;

    case AJP_CODE_CPONG_REPLY:
        /* XXX */
        break;
    }

    error = g_error_new_literal(ajp_client_quark(), 0,
                                "unknown packet from AJP server");
    ajp_client_abort_response(client, error);
    return false;
}

/**
 * Consume response body chunk data.
 *
 * @return the number of bytes consumed
 */
static size_t
ajp_consume_body_chunk(struct ajp_client *client,
                       const void *data, size_t length)
{
    assert(client->response.read_state == ajp_client::Response::READ_BODY);
    assert(client->response.chunk_length > 0);
    assert(data != nullptr);
    assert(length > 0);

    if (length > client->response.chunk_length)
        length = client->response.chunk_length;

    size_t nbytes = istream_invoke_data(&client->response_body, data, length);
    if (nbytes > 0) {
        client->response.chunk_length -= nbytes;
        client->response.remaining -= nbytes;
    }

    return nbytes;
}

/**
 * Discard junk data after a response body chunk.
 *
 * @return the number of bytes consumed
 */
static size_t
ajp_consume_body_junk(struct ajp_client *client, size_t length)
{
    assert(client->response.read_state == ajp_client::Response::READ_BODY ||
           client->response.read_state == ajp_client::Response::READ_NO_BODY);
    assert(client->response.chunk_length == 0);
    assert(client->response.junk_length > 0);
    assert(length > 0);

    if (length > client->response.junk_length)
        length = client->response.junk_length;

    client->response.junk_length -= length;
    return length;
}

/**
 * Handle the remaining data in the input buffer.
 */
static BufferedResult
ajp_client_feed(struct ajp_client *client,
                const uint8_t *data, const size_t length)
{
    assert(client != nullptr);
    assert(client->response.read_state == ajp_client::Response::READ_BEGIN ||
           client->response.read_state == ajp_client::Response::READ_NO_BODY ||
           client->response.read_state == ajp_client::Response::READ_BODY);
    assert(data != nullptr);
    assert(length > 0);

    const uint8_t *const end = data + length;

    do {
        if (client->response.read_state == ajp_client::Response::READ_BODY ||
            client->response.read_state == ajp_client::Response::READ_NO_BODY) {
            /* there is data left from the previous body chunk */
            if (client->response.chunk_length > 0) {
                const size_t remaining = end - data;
                size_t nbytes = ajp_consume_body_chunk(client, data,
                                                       remaining);
                if (nbytes == 0)
                    return client->socket.IsValid()
                        ? BufferedResult::BLOCKING
                        : BufferedResult::CLOSED;

                data += nbytes;
                client->socket.Consumed(nbytes);
                if (data == end || client->response.chunk_length > 0)
                    /* want more data */
                    return nbytes < remaining
                        ? BufferedResult::PARTIAL
                        : BufferedResult::MORE;
            }

            if (client->response.junk_length > 0) {
                size_t nbytes = ajp_consume_body_junk(client, end - data);
                assert(nbytes > 0);

                data += nbytes;
                client->socket.Consumed(nbytes);
                if (data == end || client->response.chunk_length > 0)
                    /* want more data */
                    return BufferedResult::MORE;
            }
        }

        if (data + sizeof(struct ajp_header) + 1 > end)
            /* we need a full header */
            return BufferedResult::MORE;

        const struct ajp_header *header = (const struct ajp_header*)data;
        size_t header_length = FromBE16(header->length);

        if (header->a != 'A' || header->b != 'B' || header_length == 0) {
            GError *error =
                g_error_new_literal(ajp_client_quark(), 0,
                                    "malformed AJP response packet");
            ajp_client_abort_response(client, error);
            return BufferedResult::CLOSED;
        }

        const ajp_code code = (ajp_code)data[sizeof(*header)];

        if (code == AJP_CODE_SEND_BODY_CHUNK) {
            const struct ajp_send_body_chunk *chunk =
                (const struct ajp_send_body_chunk *)(header + 1);

            if (client->response.read_state != ajp_client::Response::READ_BODY &&
                client->response.read_state != ajp_client::Response::READ_NO_BODY) {
                GError *error =
                    g_error_new_literal(ajp_client_quark(), 0,
                                        "unexpected SEND_BODY_CHUNK packet from AJP server");
                ajp_client_abort_response(client, error);
                return BufferedResult::CLOSED;
            }

            const size_t nbytes = sizeof(*header) + sizeof(*chunk);
            if (data + nbytes > end)
                /* we need the chunk length */
                return BufferedResult::MORE;

            client->response.chunk_length = FromBE16(chunk->length);
            if (sizeof(*chunk) + client->response.chunk_length > header_length) {
                GError *error =
                    g_error_new_literal(ajp_client_quark(), 0,
                                        "malformed AJP SEND_BODY_CHUNK packet");
                ajp_client_abort_response(client, error);
                return BufferedResult::CLOSED;
            }

            client->response.junk_length = header_length - sizeof(*chunk) - client->response.chunk_length;

            if (client->response.read_state == ajp_client::Response::READ_NO_BODY) {
                /* discard all response body chunks after HEAD request */
                client->response.junk_length += client->response.chunk_length;
                client->response.chunk_length = 0;
            }

            if (client->response.remaining >= 0 &&
                (off_t)client->response.chunk_length > client->response.remaining) {
                GError *error =
                    g_error_new_literal(ajp_client_quark(), 0,
                                        "excess chunk length in AJP SEND_BODY_CHUNK packet");
                ajp_client_abort_response(client, error);
                return BufferedResult::CLOSED;
            }

            /* consume the body chunk header and start sending the
               body */
            client->socket.Consumed(nbytes);
            data += nbytes;
            continue;
        }

        const size_t nbytes = sizeof(*header) + header_length;

        if (data + nbytes > end)
            /* the packet is not complete yet */
            return BufferedResult::MORE;

        client->socket.Consumed(nbytes);

        if (!ajp_consume_packet(client, code,
                                data + sizeof(*header) + 1,
                                header_length - 1))
            return BufferedResult::CLOSED;

        data += nbytes;
    } while (data != end);

    return BufferedResult::MORE;
}

/*
 * istream handler for the request
 *
 */

static size_t
ajp_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    assert(client != nullptr);
    assert(client->socket.IsConnected());
    assert(client->request.istream != nullptr);
    assert(data != nullptr);
    assert(length > 0);

    client->request.got_data = true;

    ssize_t nbytes = client->socket.Write(data, length);
    if (likely(nbytes >= 0)) {
        ajp_client_schedule_write(client);
        return (size_t)nbytes;
    }

    if (likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED))
        return 0;

    GError *error =
        g_error_new(ajp_client_quark(), 0,
                    "write error on AJP client connection: %s",
                    strerror(errno));
    ajp_client_abort_response(client, error);
    return 0;
}

static ssize_t
ajp_request_stream_direct(istream_direct type, int fd, size_t max_length,
                          void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    assert(client != nullptr);
    assert(client->socket.IsConnected());
    assert(client->request.istream != nullptr);

    client->request.got_data = true;

    ssize_t nbytes = client->socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        ajp_client_schedule_write(client);
    else if (nbytes == WRITE_BLOCKING)
        return ISTREAM_RESULT_BLOCKING;
    else if (nbytes == WRITE_DESTROYED)
        return ISTREAM_RESULT_CLOSED;
    else if (nbytes < 0 && errno == EAGAIN) {
        client->request.got_data = false;
        client->socket.UnscheduleWrite();
    }

    return nbytes;
}

static void
ajp_request_stream_eof(void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    client->socket.UnscheduleWrite();
    client->socket.Read(true);
}

static void
ajp_request_stream_abort(GError *error, void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    if (client->response.read_state == ajp_client::Response::READ_END)
        /* this is a recursive call, this object is currently being
           destructed further up the stack */
        return;

    g_prefix_error(&error, "AJP request stream failed: ");
    ajp_client_abort_response(client, error);
}

static const struct istream_handler ajp_request_stream_handler = {
    .data = ajp_request_stream_data,
    .direct = ajp_request_stream_direct,
    .eof = ajp_request_stream_eof,
    .abort = ajp_request_stream_abort,
};

/*
 * socket_wrapper handler
 *
 */

static BufferedResult
ajp_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    const ScopePoolRef ref(*client->pool TRACE_ARGS);
    return ajp_client_feed(client, (const uint8_t *)buffer, size);
}

static bool
ajp_client_socket_closed(void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    /* the rest of the response may already be in the input buffer */
    ajp_client_release_socket(client, false);
    return true;
}

static bool
ajp_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    struct ajp_client *client = (struct ajp_client *)ctx;

    /* only READ_BODY could have blocked */
    assert(client->response.read_state == ajp_client::Response::READ_BODY);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static bool
ajp_client_socket_write(void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    const ScopePoolRef ref(*client->pool TRACE_ARGS);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = client->socket.IsValid() &&
        client->socket.IsConnected();
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            ajp_client_schedule_write(client);
        else
            client->socket.UnscheduleWrite();
    }

    return result;
}

static void
ajp_client_socket_error(GError *error, void *ctx)
{
    struct ajp_client *client = (struct ajp_client *)ctx;

    g_prefix_error(&error, "AJP connection failed: ");
    ajp_client_abort_response(client, error);
}

static constexpr BufferedSocketHandler ajp_client_socket_handler = {
    .data = ajp_client_socket_data,
    .closed = ajp_client_socket_closed,
    .remaining = ajp_client_socket_remaining,
    .write = ajp_client_socket_write,
    .error = ajp_client_socket_error,
};

/*
 * async operation
 *
 */

static struct ajp_client *
async_to_ajp_connection(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &ajp_client::request_async);
}

static void
ajp_client_request_abort(struct async_operation *ao)
{
    struct ajp_client *client
        = async_to_ajp_connection(ao);

    /* async_operation_rerf::Abort() can only be used before the
       response was delivered to our callback */
    assert(client->response.read_state == ajp_client::Response::READ_BEGIN ||
           client->response.read_state == ajp_client::Response::READ_NO_BODY);

    client->response.read_state = ajp_client::Response::READ_END;
    ajp_client_release(client, false);
}

static const struct async_operation_class ajp_client_request_async_operation = {
    .abort = ajp_client_request_abort,
};


/*
 * constructor
 *
 */

void
ajp_client_request(struct pool *pool, int fd, enum istream_direct fd_type,
                   const struct lease *lease, void *lease_ctx,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   struct strmap *headers,
                   struct istream *body,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref)
{
    assert(protocol != nullptr);
    assert(http_method_is_valid(method));

    if (!uri_path_verify_quick(uri)) {
        lease->Release(lease_ctx, true);
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new(ajp_client_quark(), 0,
                        "malformed request URI '%s'", uri);
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    pool_ref(pool);
    auto client = NewFromPool<struct ajp_client>(*pool);
    client->pool = pool;

    client->socket.Init(*pool, fd, fd_type,
                        &ajp_client_timeout, &ajp_client_timeout,
                        ajp_client_socket_handler, client);

    p_lease_ref_set(client->lease_ref, *lease, lease_ctx,
                    *pool, "ajp_client_lease");

    GrowingBuffer *gb = growing_buffer_new(pool, 256);

    struct ajp_header *header = (struct ajp_header *)
        growing_buffer_write(gb, sizeof(*header));
    header->a = 0x12;
    header->b = 0x34;

    const enum ajp_method ajp_method = to_ajp_method(method);
    if (ajp_method == AJP_METHOD_NULL) {
        /* invalid or unknown method */
        p_lease_release(client->lease_ref, true, *client->pool);
        if (body != nullptr)
            istream_close_unused(body);

        GError *error =
            g_error_new_literal(ajp_client_quark(), 0,
                                "unknown request method");
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    struct {
        uint8_t prefix_code, method;
    } prefix_and_method;
    prefix_and_method.prefix_code = AJP_CODE_FORWARD_REQUEST;
    prefix_and_method.method = (uint8_t)ajp_method;

    growing_buffer_write_buffer(gb, &prefix_and_method, sizeof(prefix_and_method));

    const char *query_string = strchr(uri, '?');
    size_t uri_length = query_string != nullptr
        ? (size_t)(query_string - uri)
        : strlen(uri);

    serialize_ajp_string(gb, protocol);
    serialize_ajp_string_n(gb, uri, uri_length);
    serialize_ajp_string(gb, remote_addr);
    serialize_ajp_string(gb, remote_host);
    serialize_ajp_string(gb, server_name);
    serialize_ajp_integer(gb, server_port);
    serialize_ajp_bool(gb, is_ssl);

    GrowingBuffer *headers_buffer = nullptr;
    unsigned num_headers = 0;
    if (headers != nullptr) {
        /* serialize the request headers - note that
           serialize_ajp_headers() ignores the Content-Length header,
           we will append it later */
        headers_buffer = growing_buffer_new(pool, 2048);
        num_headers = serialize_ajp_headers(headers_buffer, headers);
    }

    /* Content-Length */

    if (body != nullptr)
        ++num_headers;

    serialize_ajp_integer(gb, num_headers);
    if (headers != nullptr)
        growing_buffer_cat(gb, headers_buffer);

    off_t available = -1;

    size_t requested;
    if (body != nullptr) {
        available = istream_available(body, false);
        if (available == -1) {
            /* AJPv13 does not support chunked request bodies */
            p_lease_release(client->lease_ref, true, *client->pool);
            istream_close_unused(body);

            GError *error =
                g_error_new_literal(ajp_client_quark(), 0,
                                    "AJPv13 does not support chunked request bodies");
            handler->InvokeAbort(handler_ctx, error);
            return;
        }

        if (available == 0)
            istream_free_unused(&body);
        else
            requested = 1024;
    }

    if (available >= 0) {
        char buffer[32];
        format_uint64(buffer, (uint64_t)available);
        serialize_ajp_integer(gb, AJP_HEADER_CONTENT_LENGTH);
        serialize_ajp_string(gb, buffer);
    }

    /* attributes */

    if (query_string != nullptr) {
        char name = AJP_ATTRIBUTE_QUERY_STRING;
        growing_buffer_write_buffer(gb, &name, sizeof(name));
        serialize_ajp_string(gb, query_string + 1); /* skip the '?' */
    }

    growing_buffer_write_buffer(gb, "\xff", 1);

    /* XXX is this correct? */

    header->length = ToBE16(growing_buffer_size(gb) - sizeof(*header));

    struct istream *request = istream_gb_new(pool, gb);
    if (body != nullptr) {
        client->request.ajp_body = istream_ajp_body_new(pool, body);
        istream_ajp_body_request(client->request.ajp_body, requested);
        request = istream_cat_new(pool, request, client->request.ajp_body,
                                  istream_memory_new(pool, &empty_body_chunk,
                                                     sizeof(empty_body_chunk)),
                                  nullptr);
    } else {
        client->request.ajp_body = nullptr;
    }

    istream_assign_handler(&client->request.istream, request,
                           &ajp_request_stream_handler, client,
                           client->socket.GetDirectMask());

    client->request.handler.Set(*handler, handler_ctx);

    client->request_async.Init(ajp_client_request_async_operation);
    async_ref->Set(client->request_async);

    /* XXX append request body */

    client->response.read_state = ajp_client::Response::READ_BEGIN;
    client->response.no_body = http_method_is_empty(method);
    client->response.in_handler = false;

    client->socket.ScheduleReadNoTimeout(true);
    istream_read(client->request.istream);
}
