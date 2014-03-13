/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached_client.hxx"
#include "memcached_packet.hxx"
#include "buffered_socket.h"
#include "please.h"
#include "async.h"
#include "pevent.h"
#include "istream-internal.h"
#include "buffered_io.h"
#include "fifo-buffer.h"

#include <daemon/log.h>

#include <glib.h>

#include <errno.h>
#include <sys/socket.h>
#include <string.h>

struct memcached_client {
    enum class ReadState {
        HEADER,
        EXTRAS,
        KEY,
        VALUE,
        END,
    };

    struct pool *pool, *caller_pool;

    /* I/O */
    struct buffered_socket socket;
    struct lease_ref lease_ref;

    /* request */
    struct {
        const struct memcached_client_handler *handler;
        void *handler_ctx;

        struct async_operation async;

        struct istream *istream;
    } request;

    /* response */
    struct {
        ReadState read_state;

        /**
         * This flag is true if we are currently calling the
         * #memcached_client_handler.  During this period,
         * memcached_client_socket_read() does nothing, to prevent
         * recursion.
         */
        bool in_handler;

        struct memcached_response_header header;

        unsigned char *extras;

        struct {
            void *buffer;
            unsigned char *tail;
            size_t remaining;
        } key;

        struct istream value;

        /**
         * Total number of bytes remaining to read from the response,
         * including extras and key.
         */
        size_t remaining;
    } response;
};

static const struct timeval memcached_client_timeout = {
    .tv_sec = 5,
    .tv_usec = 0,
};

static inline bool
memcached_connection_valid(const struct memcached_client *client)
{
    return buffered_socket_valid(&client->socket);
}

static inline bool
memcached_client_check_direct(const struct memcached_client *client)
{
    assert(buffered_socket_connected(&client->socket));
    assert(client->response.read_state == memcached_client::ReadState::VALUE);

    return istream_check_direct(&client->response.value,
                                client->socket.base.fd_type);
}

static void
memcached_client_schedule_write(struct memcached_client *client)
{
    buffered_socket_schedule_write(&client->socket);
}

/**
 * Release the socket held by this object.
 */
static void
memcached_client_release_socket(struct memcached_client *client, bool reuse)
{
    assert(client != nullptr);

    buffered_socket_abandon(&client->socket);
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

static void
memcached_client_destroy_socket(struct memcached_client *client, bool reuse)
{
    assert(client != nullptr);

    if (buffered_socket_connected(&client->socket))
        memcached_client_release_socket(client, reuse);
    buffered_socket_destroy(&client->socket);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
memcached_client_release(struct memcached_client *client, bool reuse)
{
    assert(client != nullptr);

    if (buffered_socket_valid(&client->socket))
        memcached_client_destroy_socket(client, reuse);

    pool_unref(client->pool);
}

static void
memcached_connection_abort_response_header(struct memcached_client *client,
                                           GError *error)
{
    assert(client != nullptr);
    assert(client->response.read_state == memcached_client::ReadState::HEADER ||
           client->response.read_state == memcached_client::ReadState::EXTRAS ||
           client->response.read_state == memcached_client::ReadState::KEY);

    async_operation_finished(&client->request.async);

    if (buffered_socket_valid(&client->socket))
        memcached_client_destroy_socket(client, false);

    client->request.handler->error(error, client->request.handler_ctx);
    pool_unref(client->caller_pool);

    client->response.read_state = memcached_client::ReadState::END;

    if (client->request.istream != nullptr)
        istream_free_handler(&client->request.istream);

    pool_unref(client->pool);
}

static void
memcached_connection_abort_response_value(struct memcached_client *client,
                                          GError *error)
{
    assert(client != nullptr);
    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    if (buffered_socket_valid(&client->socket))
        memcached_client_destroy_socket(client, false);

    client->response.read_state = memcached_client::ReadState::END;
    istream_deinit_abort(&client->response.value, error);

    pool_unref(client->caller_pool);
    pool_unref(client->pool);
}

static void
memcached_connection_abort_response(struct memcached_client *client,
                                    GError *error)
{
    assert(client->response.read_state != memcached_client::ReadState::END);

    switch (client->response.read_state) {
    case memcached_client::ReadState::HEADER:
    case memcached_client::ReadState::EXTRAS:
    case memcached_client::ReadState::KEY:
        memcached_connection_abort_response_header(client, error);
        return;

    case memcached_client::ReadState::VALUE:
        memcached_connection_abort_response_value(client, error);
        return;

    case memcached_client::ReadState::END:
        assert(false);
        break;
    }

    /* unreachable */
    assert(false);
}

/*
 * response value istream
 *
 */

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wextended-offsetof"
#endif

static inline struct memcached_client *
istream_to_memcached_client(struct istream *istream)
{
    void *p = ((char *)istream) - offsetof(struct memcached_client, response.value);
        return (struct memcached_client *)p;
}

static off_t
istream_memcached_available(struct istream *istream, G_GNUC_UNUSED bool partial)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    return client->response.remaining;
}

static void
istream_memcached_read(struct istream *istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    if (client->response.in_handler)
        /* avoid recursion; the memcached_client_handler caller will
           continue parsing the response if possible */
        return;

    if (buffered_socket_connected(&client->socket))
        client->socket.direct = memcached_client_check_direct(client);

    buffered_socket_read(&client->socket, true);
}

static void
istream_memcached_close(struct istream *istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);
    struct pool *caller_pool = client->caller_pool;

    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->request.istream == nullptr);

    memcached_client_release(client, false);

    istream_deinit(&client->response.value);
    pool_unref(caller_pool);
}

static const struct istream_class memcached_response_value = {
    .available = istream_memcached_available,
    .read = istream_memcached_read,
    .close = istream_memcached_close,
};

/*
 * response parser
 *
 */

static enum buffered_result
memcached_submit_response(struct memcached_client *client)
{
    assert(client->response.read_state == memcached_client::ReadState::KEY);

    async_operation_finished(&client->request.async);

    if (client->request.istream != nullptr) {
        /* at this point, the request must have been sent */
        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "memcached server sends response too early");
        memcached_connection_abort_response_header(client, error);
        return BUFFERED_CLOSED;
    }

    if (client->response.remaining > 0) {
        /* there's a value: pass it to the callback, continue
           reading */
        struct istream *value;
        bool valid;

        client->response.read_state = memcached_client::ReadState::VALUE;

        istream_init(&client->response.value, &memcached_response_value,
                     client->pool);
        value = istream_struct_cast(&client->response.value);

        pool_ref(client->pool);

        /* we need this additional reference in case the handler
           closes the body */
        pool_ref(client->caller_pool);

        client->response.in_handler = true;
        client->request.handler->response((memcached_response_status)g_ntohs(client->response.header.status),
                                          client->response.extras,
                                          client->response.header.extras_length,
                                          client->response.key.buffer,
                                          g_ntohs(client->response.header.key_length),
                                          value, client->request.handler_ctx);
        client->response.in_handler = false;

        pool_unref(client->caller_pool);

        /* check if the callback has closed the value istream */
        valid = memcached_connection_valid(client);

        if (valid && buffered_socket_connected(&client->socket))
            client->socket.direct = memcached_client_check_direct(client);

        pool_unref(client->pool);

        return valid
            ? BUFFERED_AGAIN_EXPECT
            : BUFFERED_CLOSED;
    } else {
        /* no value: invoke the callback, quit */

        memcached_client_destroy_socket(client,
                                        buffered_socket_empty(&client->socket));

        client->response.read_state = memcached_client::ReadState::END;

        client->request.handler->response((memcached_response_status)g_ntohs(client->response.header.status),
                                          client->response.extras,
                                          client->response.header.extras_length,
                                          client->response.key.buffer,
                                          g_ntohs(client->response.header.key_length),
                                          nullptr, client->request.handler_ctx);
        pool_unref(client->caller_pool);

        memcached_client_release(client, false);
        return BUFFERED_CLOSED;
    }
}

static enum buffered_result
memcached_begin_key(struct memcached_client *client)
{
    assert(client->response.read_state == memcached_client::ReadState::EXTRAS);

    client->response.read_state = memcached_client::ReadState::KEY;

    client->response.key.remaining =
        g_ntohs(client->response.header.key_length);
    if (client->response.key.remaining == 0) {
        client->response.key.buffer = nullptr;
        return memcached_submit_response(client);
    }

    client->response.key.buffer
        = client->response.key.tail
        = (unsigned char *)p_malloc(client->pool,
                                    client->response.key.remaining);

    return BUFFERED_AGAIN_EXPECT;
}

static enum buffered_result
memcached_feed_header(struct memcached_client *client,
                      const void *data, size_t length)
{
    assert(client->response.read_state == memcached_client::ReadState::HEADER);

    if (length < sizeof(client->response.header))
        /* not enough data yet */
        return BUFFERED_MORE;

    memcpy(&client->response.header, data, sizeof(client->response.header));
    buffered_socket_consumed(&client->socket, sizeof(client->response.header));

    client->response.read_state = memcached_client::ReadState::EXTRAS;

    client->response.remaining = g_ntohl(client->response.header.body_length);
    if (client->response.header.magic != MEMCACHED_MAGIC_RESPONSE ||
        g_ntohs(client->response.header.key_length) +
        client->response.header.extras_length > client->response.remaining) {
        /* protocol error: abort the connection */
        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "memcached protocol error");
        memcached_connection_abort_response_header(client, error);
        return BUFFERED_CLOSED;
    }

    if (client->response.header.extras_length == 0) {
        client->response.extras = nullptr;
        return memcached_begin_key(client);
    }

    return BUFFERED_AGAIN_EXPECT;
}

static enum buffered_result
memcached_feed_extras(struct memcached_client *client,
                      const void *data, size_t length)
{
    assert(client->response.read_state == memcached_client::ReadState::EXTRAS);
    assert(client->response.header.extras_length > 0);

    if (data == nullptr ||
        length < sizeof(client->response.header.extras_length))
        return BUFFERED_MORE;

    client->response.extras = (unsigned char *)
        p_malloc(client->pool,
                 client->response.header.extras_length);
    memcpy(client->response.extras, data,
           client->response.header.extras_length);

    buffered_socket_consumed(&client->socket,
                             client->response.header.extras_length);
    client->response.remaining -= client->response.header.extras_length;

    return memcached_begin_key(client);
}

static enum buffered_result
memcached_feed_key(struct memcached_client *client,
                   const void *data, size_t length)
{
    assert(client->response.read_state == memcached_client::ReadState::KEY);
    assert(client->response.key.remaining > 0);

    if (length > client->response.key.remaining)
        length = client->response.key.remaining;

    memcpy(client->response.key.tail, data, length);
    client->response.key.tail += length;
    client->response.key.remaining -= length;
    client->response.remaining -=
        g_ntohs(client->response.header.key_length);

    buffered_socket_consumed(&client->socket, length);

    if (client->response.key.remaining == 0)
        return memcached_submit_response(client);

    return BUFFERED_MORE;
}

static enum buffered_result
memcached_feed_value(struct memcached_client *client,
                     const void *data, size_t length)
{
    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->response.remaining > 0);

    if (buffered_socket_connected(&client->socket) &&
        length >= client->response.remaining)
        memcached_client_release_socket(client,
                                        length == client->response.remaining);

    if (length > client->response.remaining)
        length = client->response.remaining;

    size_t nbytes = istream_invoke_data(&client->response.value, data, length);
    if (nbytes == 0)
        return memcached_connection_valid(client)
            ? BUFFERED_BLOCKING
            : BUFFERED_CLOSED;

    buffered_socket_consumed(&client->socket, nbytes);

    client->response.remaining -= nbytes;
    if (client->response.remaining > 0)
        return nbytes < length
            ? BUFFERED_PARTIAL
            : BUFFERED_MORE;

    assert(!buffered_socket_connected(&client->socket));
    assert(client->request.istream == nullptr);

    client->response.read_state = memcached_client::ReadState::END;
    istream_deinit_eof(&client->response.value);
    pool_unref(client->caller_pool);

    memcached_client_release(client, false);
    return BUFFERED_CLOSED;
}

static enum buffered_result
memcached_feed(struct memcached_client *client,
               const void *data, size_t length)
{
    switch (client->response.read_state) {
    case memcached_client::ReadState::HEADER:
        return memcached_feed_header(client, data, length);

    case memcached_client::ReadState::EXTRAS:
        return memcached_feed_extras(client, data, length);

    case memcached_client::ReadState::KEY:
        return memcached_feed_key(client, data, length);

    case memcached_client::ReadState::VALUE:
        return memcached_feed_value(client, data, length);

    case memcached_client::ReadState::END:
        /* unreachable */
        assert(false);
        return BUFFERED_CLOSED;
    }

    /* unreachable */
    assert(false);
    return BUFFERED_CLOSED;
}

static enum direct_result
memcached_client_try_read_direct(struct memcached_client *client,
                                 int fd, istream_direct_t type)
{
    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->response.remaining > 0);

    ssize_t nbytes = istream_invoke_direct(&client->response.value, type, fd,
                                           client->response.remaining);
    if (likely(nbytes > 0)) {
        client->response.remaining -= nbytes;

        if (client->response.remaining == 0) {
            memcached_client_destroy_socket(client, true);
            istream_deinit_eof(&client->response.value);
            pool_unref(client->caller_pool);
            pool_unref(client->pool);
            return DIRECT_CLOSED;
        } else
            return DIRECT_OK;
    } else if (unlikely(nbytes == ISTREAM_RESULT_EOF)) {
        return DIRECT_END;
    } else if (nbytes == ISTREAM_RESULT_BLOCKING) {
        return DIRECT_BLOCKING;
    } else if (nbytes == ISTREAM_RESULT_CLOSED) {
        return DIRECT_CLOSED;
    } else if (errno == EAGAIN) {
        return DIRECT_EMPTY;
    } else {
        return DIRECT_ERRNO;
    }
}

/*
 * socket_wrapper handler
 *
 */

/**
 * The libevent callback for sending the request to the socket.
 */
static bool
memcached_client_socket_write(void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;
    assert(client->response.read_state != memcached_client::ReadState::END);

    pool_ref(client->pool);

    istream_read(client->request.istream);

    bool result = buffered_socket_valid(&client->socket) &&
        buffered_socket_connected(&client->socket);
    pool_unref(client->pool);
    return result;
}

static enum buffered_result
memcached_client_socket_data(const void *buffer, size_t size, void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;
    assert(client->response.read_state != memcached_client::ReadState::END);

    pool_ref(client->pool);
    enum buffered_result result = memcached_feed(client, buffer, size);
    pool_unref(client->pool);
    return result;
}

static enum direct_result
memcached_client_socket_direct(int fd, enum istream_direct type, void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;
    assert(client->response.read_state == memcached_client::ReadState::VALUE);
    assert(client->response.remaining > 0);
    assert(memcached_client_check_direct(client));

    return memcached_client_try_read_direct(client, fd, type);
}

static bool
memcached_client_socket_closed(void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;

    /* the rest of the response may already be in the input buffer */
    memcached_client_release_socket(client, false);
    return true;
}

static bool
memcached_client_socket_remaining(gcc_unused size_t remaining, void *ctx)
{
    gcc_unused
    memcached_client *client = (memcached_client *)ctx;

    /* only READ_VALUE could have blocked */
    assert(client->response.read_state == memcached_client::ReadState::VALUE);

    /* the rest of the response may already be in the input buffer */
    return true;
}

static void
memcached_client_socket_error(GError *error, void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;

    g_prefix_error(&error, "memcached connection failed: ");
    memcached_connection_abort_response(client, error);
}

static const struct buffered_socket_handler memcached_client_socket_handler = {
    .data = memcached_client_socket_data,
    .direct = memcached_client_socket_direct,
    .closed = memcached_client_socket_closed,
    .remaining = memcached_client_socket_remaining,
    .write = memcached_client_socket_write,
    .error = memcached_client_socket_error,
};

/*
 * istream handler for the request
 *
 */

static size_t
memcached_request_stream_data(const void *data, size_t length, void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;

    assert(client->request.istream != nullptr);
    assert(client->response.read_state == memcached_client::ReadState::HEADER ||
           client->response.read_state == memcached_client::ReadState::EXTRAS ||
           client->response.read_state == memcached_client::ReadState::KEY);
    assert(data != nullptr);
    assert(length > 0);

    ssize_t nbytes = buffered_socket_write(&client->socket, data, length);
    if (nbytes < 0) {
        if (nbytes == WRITE_BLOCKING || nbytes == WRITE_DESTROYED)
            return 0;

        GError *error =
            g_error_new(memcached_client_quark(), 0,
                        "write error on memcached connection: %s",
                        strerror(errno));
        memcached_connection_abort_response(client, error);
        return 0;
    }

    memcached_client_schedule_write(client);
    return (size_t)nbytes;
}

static void
memcached_request_stream_eof(void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;

    assert(client->request.istream != nullptr);
    assert(client->response.read_state == memcached_client::ReadState::HEADER ||
           client->response.read_state == memcached_client::ReadState::EXTRAS ||
           client->response.read_state == memcached_client::ReadState::KEY);

    client->request.istream = nullptr;

    buffered_socket_unschedule_write(&client->socket);
    buffered_socket_read(&client->socket, true);
}

static void
memcached_request_stream_abort(GError *error, void *ctx)
{
    memcached_client *client = (memcached_client *)ctx;

    assert(client->request.istream != nullptr);
    assert(client->response.read_state == memcached_client::ReadState::HEADER ||
           client->response.read_state == memcached_client::ReadState::EXTRAS ||
           client->response.read_state == memcached_client::ReadState::KEY);

    client->request.istream = nullptr;

    memcached_connection_abort_response(client, error);
}

static const struct istream_handler memcached_request_stream_handler = {
    .data = memcached_request_stream_data,
    .eof = memcached_request_stream_eof,
    .abort = memcached_request_stream_abort,
};

/*
 * async operation
 *
 */

static struct memcached_client *
async_to_memcached_client(struct async_operation *ao)
{
    void *p = ((char *)ao) - offsetof(struct memcached_client, request.async);
    return (struct memcached_client *)p;
}

static void
memcached_client_request_abort(struct async_operation *ao)
{
    struct memcached_client *client
        = async_to_memcached_client(ao);
    struct pool *caller_pool = client->caller_pool;
    struct istream *request_istream = client->request.istream;

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == memcached_client::ReadState::HEADER ||
           client->response.read_state == memcached_client::ReadState::EXTRAS ||
           client->response.read_state == memcached_client::ReadState::KEY);

    memcached_client_release(client, false);
    pool_unref(caller_pool);

    if (request_istream != nullptr)
        istream_close_handler(request_istream);
}

static const struct async_operation_class memcached_client_async_operation = {
    .abort = memcached_client_request_abort,
};

/*
 * constructor
 *
 */

void
memcached_client_invoke(struct pool *caller_pool,
                        int fd, enum istream_direct fd_type,
                        const struct lease *lease, void *lease_ctx,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        struct istream *value,
                        const struct memcached_client_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    struct istream *request;

    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    struct pool *pool = pool_new_linear(caller_pool, "memcached_client", 4096);

    request = memcached_request_packet(pool, opcode, extras, extras_length,
                                       key, key_length, value,
                                       0x1234 /* XXX? */);
    if (request == nullptr) {
        lease_direct_release(lease, lease_ctx, true);

        GError *error =
            g_error_new_literal(memcached_client_quark(), 0,
                                "failed to generate memcached request packet");
        handler->error(error, handler_ctx);
        return;
    }

    pool_ref(caller_pool);

    auto client = PoolAlloc<memcached_client>(pool);
    client->pool = pool;
    client->caller_pool = caller_pool;

    buffered_socket_init(&client->socket, pool, fd, fd_type,
                         nullptr, &memcached_client_timeout,
                         &memcached_client_socket_handler, client);

    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "memcached_client_lease");

    istream_assign_handler(&client->request.istream, request,
                           &memcached_request_stream_handler, client,
                           0);

    client->request.handler = handler;
    client->request.handler_ctx = handler_ctx;

    async_init(&client->request.async, &memcached_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    client->response.read_state = memcached_client::ReadState::HEADER;

    istream_read(client->request.istream);
}
