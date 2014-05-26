/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi_client.hxx"
#include "fcgi_quark.h"
#include "fcgi_protocol.h"
#include "fcgi_serialize.hxx"
#include "buffered_socket.hxx"
#include "growing-buffer.h"
#include "http_response.h"
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
#include "product.h"
#include "util/Cast.hxx"

#include <glib.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static inline constexpr uint16_t
ByteSwap16(uint16_t value)
{
  return (value >> 8) | (value << 8);
}

/**
 * Converts a 16bit value from the system's byte order to big endian.
 */
static inline constexpr uint16_t
ToBE16(uint16_t value)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    return value;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN 
  return ByteSwap16(value);
#else
#error Unknown byte order
#endif
#endif
}

#ifndef NDEBUG
static LIST_HEAD(fcgi_clients);
#endif

struct fcgi_client {
#ifndef NDEBUG
    struct list_head siblings;
#endif

    struct pool *pool, *caller_pool;

    BufferedSocket socket;

    struct lease_ref lease_ref;

    int stderr_fd;

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

    struct Response {
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

static constexpr struct timeval fcgi_client_timeout = {
    .tv_sec = 120,
    .tv_usec = 0,
};

static void
fcgi_client_response_body_init(struct fcgi_client *client);

/**
 * Release the socket held by this object.
 */
static void
fcgi_client_release_socket(struct fcgi_client *client, bool reuse)
{
    assert(client != nullptr);

    client->socket.Abandon();
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
fcgi_client_release(struct fcgi_client *client, bool reuse)
{
    assert(client != nullptr);

    if (client->socket.IsConnected())
        fcgi_client_release_socket(client, reuse);

    client->socket.Destroy();

    if (client->stderr_fd >= 0)
        close(client->stderr_fd);

#ifndef NDEBUG
    list_remove(&client->siblings);
#endif

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
    assert(client->response.read_state == fcgi_client::Response::READ_HEADERS ||
           client->response.read_state == fcgi_client::Response::READ_NO_BODY);

    async_operation_finished(&client->async);

    if (client->socket.IsConnected())
        fcgi_client_release_socket(client, false);

    if (client->request.istream != nullptr)
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
    assert(client->response.read_state == fcgi_client::Response::READ_BODY);

    if (client->socket.IsConnected())
        fcgi_client_release_socket(client, false);

    if (client->request.istream != nullptr)
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
    assert(client->response.read_state == fcgi_client::Response::READ_HEADERS ||
           client->response.read_state == fcgi_client::Response::READ_NO_BODY ||
           client->response.read_state == fcgi_client::Response::READ_BODY);

    if (client->response.read_state != fcgi_client::Response::READ_BODY)
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
    assert(client->response.read_state == fcgi_client::Response::READ_BODY);

    if (client->socket.IsConnected())
        fcgi_client_release_socket(client, false);

    if (client->request.istream != nullptr)
        istream_free_handler(&client->request.istream);

    istream_deinit(&client->response.body);
    fcgi_client_release(client, false);
}

/**
 * Find the #FCGI_END_REQUEST packet matching the current request, and
 * returns the offset where it ends, or 0 if none was found.
 */
gcc_pure
static size_t
fcgi_client_find_end_request(struct fcgi_client *client,
                             const uint8_t *const data0, size_t size)
{
    const uint8_t *data = data0, *const end = data0 + size;

    /* skip the rest of the current packet */
    data += client->content_length + client->skip_length;

    while (true) {
        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)data;
        data = (const uint8_t *)(header + 1);
        if (data > end)
            /* reached the end of the given buffer: not found */
            return 0;

        data += ntohs(header->content_length);
        data += header->padding_length;

        if (header->request_id == client->id &&
            header->type == FCGI_END_REQUEST)
            /* found it: return the packet end offset */
            return data - data0;
    }
}

static bool
fcgi_client_handle_line(struct fcgi_client *client,
                        const char *line, size_t length)
{
    assert(client != nullptr);
    assert(client->response.headers != nullptr);
    assert(line != nullptr);

    if (length > 0) {
        header_parse_line(client->pool, client->response.headers,
                          line, length);
        return false;
    } else {
        client->response.read_state = fcgi_client::Response::READ_BODY;
        client->response.stderr = false;
        return true;
    }
}

static size_t
fcgi_client_parse_headers(struct fcgi_client *client,
                          const char *data, size_t length)
{
    const char *p = data, *const data_end = data + length;

    const char *next = nullptr;
    bool finished = false;

    const char *eol;
    while ((eol = (const char *)memchr(p, '\n', data_end - p)) != nullptr) {
        next = eol + 1;
        --eol;
        while (eol >= p && char_is_whitespace(*eol))
            --eol;

        finished = fcgi_client_handle_line(client, p, eol - p + 1);
        if (finished)
            break;

        p = next;
    }

    return next != nullptr ? next - data : 0;
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
        ssize_t nbytes = client->stderr_fd >= 0
            ? write(client->stderr_fd, data, length)
            : fwrite(data, 1, length, stderr);
        return nbytes > 0 ? (size_t)nbytes : 0;
    }

    switch (client->response.read_state) {
        size_t consumed;

    case fcgi_client::Response::READ_HEADERS:
        return fcgi_client_parse_headers(client, (const char *)data, length);

    case fcgi_client::Response::READ_NO_BODY:
        /* unreachable */
        assert(false);
        return 0;

    case fcgi_client::Response::READ_BODY:
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
    assert(client->response.read_state == fcgi_client::Response::READ_BODY);

    http_status_t status = HTTP_STATUS_OK;

    const char *p = strmap_remove(client->response.headers,
                                  "status");
    if (p != nullptr) {
        int i = atoi(p);
        if (http_status_is_valid((http_status_t)i))
            status = (http_status_t)i;
    }

    if (http_status_is_empty(status) || client->response.no_body) {
        client->response.read_state = fcgi_client::Response::READ_NO_BODY;
        client->response.status = status;

        /* ignore the rest of this STDOUT payload */
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }

    client->response.available = -1;
    p = strmap_remove(client->response.headers,
                      "content-length");
    if (p != nullptr) {
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

    return client->socket.IsValid();
}

/**
 * Handle an END_REQUEST packet.  This function will always destroy
 * the client.
 */
static void
fcgi_client_handle_end(struct fcgi_client *client)
{
    assert(!client->socket.IsConnected());

    if (client->response.read_state == fcgi_client::Response::READ_HEADERS) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "premature end of headers "
                                "from FastCGI application");
        fcgi_client_abort_response_headers(client, error);
        return;
    }

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    if (client->response.read_state == fcgi_client::Response::READ_NO_BODY) {
        async_operation_finished(&client->async);
        http_response_handler_invoke_response(&client->handler,
                                              client->response.status,
                                              client->response.headers,
                                              nullptr);
    } else if (client->response.available > 0) {
        GError *error =
            g_error_new_literal(fcgi_quark(), 0,
                                "premature end of body "
                                "from FastCGI application");
        fcgi_client_abort_response_body(client, error);
        return;
    } else
        istream_deinit_eof(&client->response.body);

    fcgi_client_release(client, false);
}

/**
 * A packet header was received.
 *
 * @return false if the client has been destroyed
 */
static bool
fcgi_client_handle_header(struct fcgi_client *client,
                          const struct fcgi_record_header *header)
{
    client->content_length = ntohs(header->content_length);
    client->skip_length = header->padding_length;

    if (header->request_id != client->id) {
        /* wrong request id; discard this packet */
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }

    switch (header->type) {
    case FCGI_STDOUT:
        client->response.stderr = false;

        if (client->response.read_state == fcgi_client::Response::READ_NO_BODY) {
            /* ignore all payloads until #FCGI_END_REQUEST */
            client->skip_length += client->content_length;
            client->content_length = 0;
        }

        return true;

    case FCGI_STDERR:
        client->response.stderr = true;
        return true;

    case FCGI_END_REQUEST:
        fcgi_client_handle_end(client);
        return false;

    default:
        client->skip_length += client->content_length;
        client->content_length = 0;
        return true;
    }
}

/**
 * Consume data from the input buffer.
 */
static BufferedResult
fcgi_client_consume_input(struct fcgi_client *client,
                          const uint8_t *data0, size_t length0)
{
    const uint8_t *data = data0, *const end = data0 + length0;

    do {
        if (client->content_length > 0) {
            bool at_headers = client->response.read_state == fcgi_client::Response::READ_HEADERS;

            size_t length = end - data;
            if (length > client->content_length)
                length = client->content_length;

            size_t nbytes = fcgi_client_feed(client, data, length);
            if (nbytes == 0) {
                if (at_headers) {
                    /* incomplete header line received, want more
                       data */
                    assert(client->response.read_state == fcgi_client::Response::READ_HEADERS);
                    assert(client->socket.IsValid());
                    return BufferedResult::MORE;
                }

                if (!client->socket.IsValid())
                    return BufferedResult::CLOSED;

                /* the response body handler blocks, wait for it to
                   become ready */
                return BufferedResult::BLOCKING;
            }

            data += nbytes;
            client->content_length -= nbytes;
            client->socket.Consumed(nbytes);

            if (at_headers && client->response.read_state == fcgi_client::Response::READ_BODY) {
                /* the read_state has been switched from HEADERS to
                   BODY: we have to deliver the response now */

                if (!fcgi_client_submit_response(client))
                    return BufferedResult::CLOSED;

                /* continue parsing the response body from the
                   buffer */
                continue;
            }

            if (client->content_length > 0)
                return data < end && client->response.read_state != fcgi_client::Response::READ_HEADERS
                    /* some was consumed, try again later */
                    ? BufferedResult::PARTIAL
                    /* all input was consumed, want more */
                    : BufferedResult::MORE;

            continue;
        }

        if (client->skip_length > 0) {
            size_t nbytes = end - data;
            if (nbytes > client->skip_length)
                nbytes = client->skip_length;

            data += nbytes;
            client->skip_length -= nbytes;
            client->socket.Consumed(nbytes);

            if (client->skip_length > 0)
                return BufferedResult::MORE;

            continue;
        }

        const struct fcgi_record_header *header =
            (const struct fcgi_record_header *)data;
        const size_t remaining = end - data;
        if (remaining < sizeof(*header))
            return BufferedResult::MORE;

        data += sizeof(*header);
        client->socket.Consumed(sizeof(*header));

        if (!fcgi_client_handle_header(client, header))
            return BufferedResult::CLOSED;
    } while (data != end);

    return BufferedResult::MORE;
}

/*
 * istream handler for the request
 *
 */

static size_t
fcgi_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    assert(client->socket.IsConnected());
    assert(client->request.istream != nullptr);

    client->request.got_data = true;

    ssize_t nbytes = client->socket.Write(data, length);
    if (nbytes > 0)
        client->socket.ScheduleWrite();
    else if (gcc_likely(nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED))
        return 0;
    else if (nbytes < 0) {
        GError *error = g_error_new(fcgi_quark(), errno,
                                    "write to FastCGI application failed: %s",
                                    strerror(errno));
        fcgi_client_abort_response(client, error);
        return 0;
    }

    return (size_t)nbytes;
}

static ssize_t
fcgi_request_stream_direct(istream_direct type, int fd,
                           size_t max_length, void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    assert(client->socket.IsConnected());

    client->request.got_data = true;

    ssize_t nbytes = client->socket.WriteFrom(fd, type, max_length);
    if (likely(nbytes > 0))
        client->socket.ScheduleWrite();
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
fcgi_request_stream_eof(void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    client->socket.UnscheduleWrite();
}

static void
fcgi_request_stream_abort(GError *error, void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    assert(client->request.istream != nullptr);

    client->request.istream = nullptr;

    g_prefix_error(&error, "FastCGI request stream failed: ");
    fcgi_client_abort_response(client, error);
}

static constexpr struct istream_handler fcgi_request_stream_handler = {
    .data = fcgi_request_stream_data,
    .direct = fcgi_request_stream_direct,
    .eof = fcgi_request_stream_eof,
    .abort = fcgi_request_stream_abort,
};


/*
 * istream implementation for the response body
 *
 */

static inline struct fcgi_client *
response_stream_to_client(struct istream *istream)
{
    return ContainerCast(istream, struct fcgi_client, response.body);
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

    client->socket.Read(true);
}

static void
fcgi_client_response_body_close(struct istream *istream)
{
    struct fcgi_client *client = response_stream_to_client(istream);

    fcgi_client_close_response_body(client);
}

static constexpr struct istream_class fcgi_client_response_body = {
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

static BufferedResult
fcgi_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    if (client->socket.IsConnected()) {
        /* check if the #FCGI_END_REQUEST packet can be found in the
           following data chunk */
        size_t offset =
            fcgi_client_find_end_request(client,
                                         (const uint8_t *)buffer, size);
        if (offset > 0)
            /* found it: we no longer need the socket, everything we
               need is already in the given buffer */
            fcgi_client_release_socket(client, offset == size);
    }

    pool_ref(client->pool);
    const BufferedResult result =
        fcgi_client_consume_input(client, (const uint8_t *)buffer, size);
    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_closed(void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    /* the rest of the response may already be in the input buffer */
    fcgi_client_release_socket(client, false);
    return true;
}

static bool
fcgi_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    /* only READ_BODY could have blocked */
    assert(client->response.read_state == fcgi_client::Response::READ_BODY);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static bool
fcgi_client_socket_write(void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    pool_ref(client->pool);

    client->request.got_data = false;
    istream_read(client->request.istream);

    const bool result = client->socket.IsValid();
    if (result && client->request.istream != nullptr) {
        if (client->request.got_data)
            client->socket.ScheduleWrite();
        else
            client->socket.UnscheduleWrite();
    }

    pool_unref(client->pool);
    return result;
}

static bool
fcgi_client_socket_timeout(void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    GError *error = g_error_new_literal(fcgi_quark(), 0, "timeout");
    fcgi_client_abort_response(client, error);
    return false;
}

static void
fcgi_client_socket_error(GError *error, void *ctx)
{
    struct fcgi_client *client = (struct fcgi_client *)ctx;

    fcgi_client_abort_response(client, error);
}

static constexpr BufferedSocketHandler fcgi_client_socket_handler = {
    .data = fcgi_client_socket_data,
    .closed = fcgi_client_socket_closed,
    .remaining = fcgi_client_socket_remaining,
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
    return ContainerCast(ao, struct fcgi_client, async);
}

static void
fcgi_client_request_abort(struct async_operation *ao)
{
    struct fcgi_client *client
        = async_to_fcgi_client(ao);

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == fcgi_client::Response::READ_HEADERS ||
           client->response.read_state == fcgi_client::Response::READ_NO_BODY);

    if (client->request.istream != nullptr)
        istream_close_handler(client->request.istream);

    fcgi_client_release(client, false);
}

static constexpr struct async_operation_class fcgi_client_async_operation = {
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
                    int stderr_fd,
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
    static constexpr struct fcgi_begin_request begin_request = {
        .role = ToBE16(FCGI_RESPONDER),
        .flags = FCGI_KEEP_CONN,
    };

    assert(http_method_is_valid(method));

    struct pool *pool = pool_new_linear(caller_pool, "fcgi_client_request",
                                        2048);
    auto client = NewFromPool<struct fcgi_client>(pool);
#ifndef NDEBUG
    list_add(&client->siblings, &fcgi_clients);
#endif
    client->pool = pool;
    pool_ref(caller_pool);
    client->caller_pool = caller_pool;

    client->socket.Init(pool, fd, fd_type,
                        &fcgi_client_timeout, &fcgi_client_timeout,
                        &fcgi_client_socket_handler, client);

    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "fcgi_client_lease");

    client->stderr_fd = stderr_fd;

    http_response_handler_set(&client->handler, handler, handler_ctx);

    async_init(&client->async, &fcgi_client_async_operation);
    async_ref_set(async_ref, &client->async);

    client->id = header.request_id;

    client->response.read_state = fcgi_client::Response::READ_HEADERS;
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
                          "SERVER_SOFTWARE", PRODUCT_TOKEN,
                          nullptr);

    if (remote_addr != nullptr)
        fcgi_serialize_params(buffer, header.request_id,
                              "REMOTE_ADDR", remote_addr,
                              nullptr);

    off_t available = body != nullptr
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
                              content_type != nullptr ? "CONTENT_TYPE" : nullptr,
                              content_type,
                              nullptr);
    }

    if (headers != nullptr)
        fcgi_serialize_headers(buffer, header.request_id, headers);

    if (num_params > 0)
        fcgi_serialize_vparams(buffer, header.request_id, params, num_params);

    header.type = FCGI_PARAMS;
    header.content_length = htons(0);
    growing_buffer_write_buffer(buffer, &header, sizeof(header));

    struct istream *request;

    if (body != nullptr)
        /* format the request body */
        request = istream_cat_new(pool,
                                  istream_gb_new(pool, buffer),
                                  istream_fcgi_new(pool, body,
                                                   header.request_id),
                                  nullptr);
    else {
        /* no request body - append an empty STDIN packet */
        header.type = FCGI_STDIN;
        header.content_length = htons(0);
        growing_buffer_write_buffer(buffer, &header, sizeof(header));

        request = istream_gb_new(pool, buffer);
    }

    istream_assign_handler(&client->request.istream, request,
                           &fcgi_request_stream_handler, client,
                           client->socket.GetDirectMask());

    client->socket.ScheduleReadNoTimeout(true);
    istream_read(client->request.istream);
}
