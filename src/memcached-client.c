/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached-client.h"
#include "memcached-packet.h"
#include "please.h"
#include "async.h"
#include "pevent.h"
#include "istream-internal.h"
#include "buffered-io.h"

#include <daemon/log.h>

#include <glib.h>

#include <errno.h>
#include <sys/socket.h>
#include <string.h>

struct memcached_client {
    pool_t pool;

    /* I/O */
    int fd;
    enum istream_direct fd_type;
    struct lease_ref lease_ref;

    /* request */
    struct {
        struct event event;

        memcached_response_handler_t handler;
        void *handler_ctx;

        struct async_operation async;

        istream_t istream;
    } request;

    /* response */
    struct {
        struct event event;

        enum {
            READ_HEADER,
            READ_EXTRAS,
            READ_KEY,
            READ_VALUE,
            READ_END,
        } read_state;

        struct memcached_response_header header;

        struct fifo_buffer *input;

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
    return client->response.input != NULL;
}

static void
memcached_client_schedule_read(struct memcached_client *client)
{
    assert(client->fd >= 0);

    p_event_add(&client->response.event,
                client->request.istream != NULL
                ? NULL : &memcached_client_timeout,
                client->pool, "memcached_client_response");
}

static void
memcached_client_schedule_write(struct memcached_client *client)
{
    assert(client->fd >= 0);

    p_event_add(&client->request.event, &memcached_client_timeout,
                client->pool, "memcached_client_request");
}

/**
 * Release the socket held by this object.
 */
static void
memcached_client_release_socket(struct memcached_client *client, bool reuse)
{
    assert(client != NULL);
    assert(client->fd >= 0);

    p_event_del(&client->request.event, client->pool);
    p_event_del(&client->response.event, client->pool);
    client->fd = -1;
    p_lease_release(&client->lease_ref, reuse, client->pool);
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
memcached_client_release(struct memcached_client *client, bool reuse)
{
    assert(client != NULL);

    client->response.input = NULL;

    if (client->fd >= 0)
        memcached_client_release_socket(client, reuse);

    pool_unref(client->pool);
}

static void
memcached_connection_abort_response_header(struct memcached_client *client)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_HEADER ||
           client->response.read_state == READ_EXTRAS ||
           client->response.read_state == READ_KEY);

    pool_ref(client->pool);

    memcached_client_release(client, false);

    client->request.handler(-1, NULL, 0, NULL, 0, NULL,
                            client->request.handler_ctx);
    client->response.read_state = READ_END;

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    pool_unref(client->pool);
}

static void
memcached_connection_abort_response_value(struct memcached_client *client)
{
    assert(client != NULL);
    assert(client->response.read_state == READ_VALUE);
    assert(client->request.istream == NULL);

    pool_ref(client->pool);

    memcached_client_release(client, false);

    client->response.read_state = READ_END;
    istream_deinit_abort(&client->response.value);

    pool_unref(client->pool);
}

static void
memcached_connection_close(struct memcached_client *client)
{
    switch (client->response.read_state) {
    case READ_HEADER:
    case READ_EXTRAS:
    case READ_KEY:
        memcached_connection_abort_response_header(client);
        return;

    case READ_VALUE:
        memcached_connection_abort_response_value(client);
        break;

    case READ_END:
        memcached_client_release(client, false);
        break;
    }
}

static bool
memcached_consume_value(struct memcached_client *client);

static void
memcached_client_try_read_direct(struct memcached_client *client);

static bool
memcached_client_fill_buffer(struct memcached_client *client);

/*
 * response value istream
 *
 */

static inline struct memcached_client *
istream_to_memcached_client(istream_t istream)
{
    return (struct memcached_client *)(((char*)istream) - offsetof(struct memcached_client, response.value));
}

static off_t
istream_memcached_available(istream_t istream, G_GNUC_UNUSED bool partial)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == READ_VALUE);
    assert(client->request.istream == NULL);

    return client->response.remaining;
}

static void
istream_memcached_read(istream_t istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == READ_VALUE);
    assert(client->request.istream == NULL);

    if (!fifo_buffer_empty(client->response.input))
        memcached_consume_value(client);
    else if (client->response.read_state == READ_VALUE &&
             istream_check_direct(&client->response.value, client->fd_type))
        memcached_client_try_read_direct(client);
    else if (memcached_client_fill_buffer(client))
        memcached_consume_value(client);
}

static void
istream_memcached_close(istream_t istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == READ_VALUE);
    assert(client->request.istream == NULL);

    istream_deinit_abort(&client->response.value);
    memcached_client_release(client, false);
}

static const struct istream memcached_response_value = {
    .available = istream_memcached_available,
    .read = istream_memcached_read,
    .close = istream_memcached_close,
};

/*
 * response parser
 *
 */

/**
 * @return true if the stream is still open and non-blocking (header
 * finished or more data is needed)
 */
static bool
memcached_consume_header(struct memcached_client *client)
{
    size_t length;
    const void *data = fifo_buffer_read(client->response.input, &length);

    assert(client->response.read_state == READ_HEADER);

    if (data == NULL || length < sizeof(client->response.header))
        /* not enough data yet */
        return true;

    memcpy(&client->response.header, data, sizeof(client->response.header));
    fifo_buffer_consume(client->response.input,
                        sizeof(client->response.header));
    client->response.read_state = READ_EXTRAS;

    client->response.remaining = g_ntohl(client->response.header.body_length);
    if (client->response.header.magic != MEMCACHED_MAGIC_RESPONSE ||
        g_ntohs(client->response.header.key_length) +
        client->response.header.extras_length > client->response.remaining) {
        /* protocol error: abort the connection */
        memcached_connection_abort_response_header(client);
        return false;
    }

    return true;
}

/**
 * @return true if the stream is still open and non-blocking (extras
 * finished or more data is needed)
 */
static bool
memcached_consume_extras(struct memcached_client *client)
{
    assert(client->response.read_state == READ_EXTRAS);

    if (client->response.header.extras_length > 0) {
        size_t length;
        const void *data = fifo_buffer_read(client->response.input, &length);

        if (data == NULL ||
            length < sizeof(client->response.header.extras_length))
            return true;

        client->response.extras = p_malloc(client->pool,
                                           client->response.header.extras_length);
        memcpy(client->response.extras, data,
               client->response.header.extras_length);

        fifo_buffer_consume(client->response.input,
                            client->response.header.extras_length);
        client->response.remaining -= client->response.header.extras_length;
    } else
        client->response.extras = NULL;

    client->response.read_state = READ_KEY;
    client->response.key.remaining =
        g_ntohs(client->response.header.key_length);
    if (client->response.key.remaining > 0)
        client->response.key.buffer
            = client->response.key.tail
            = p_malloc(client->pool, client->response.key.remaining);
    else
        client->response.key.buffer = NULL;

    return true;
}

/**
 * @return true if the stream is still open and non-blocking (key
 * finished or more data is needed)
 */
static bool
memcached_consume_key(struct memcached_client *client)
{
    assert(client->response.read_state == READ_KEY);

    if (client->response.key.remaining > 0) {
        size_t length;
        const void *data = fifo_buffer_read(client->response.input, &length);

        if (data == NULL)
            return true;

        if (length > client->response.key.remaining)
            length = client->response.key.remaining;

        memcpy(client->response.key.tail, data, length);
        client->response.key.tail += length;
        client->response.key.remaining -= length;
        client->response.remaining -=
            g_ntohs(client->response.header.key_length);

        fifo_buffer_consume(client->response.input, length);

        if (client->response.key.remaining > 0)
            return true;
    }

    if (client->request.istream != NULL) {
        /* at this point, the request must have been sent */
        daemon_log(2, "memcached server sends response too early\n");
        memcached_connection_abort_response_header(client);
        return false;
    }

    if (client->response.remaining > 0) {
        /* there's a value: pass it to the callback, continue
           reading */
        istream_t value;
        bool valid;

        if (fifo_buffer_empty(client->response.input))
            memcached_client_schedule_read(client);

        client->response.read_state = READ_VALUE;

        istream_init(&client->response.value, &memcached_response_value,
                     client->pool);
        value = istream_struct_cast(&client->response.value);

        pool_ref(client->pool);
        client->request.handler(g_ntohs(client->response.header.status),
                                client->response.extras,
                                client->response.header.extras_length,
                                client->response.key.buffer,
                                g_ntohs(client->response.header.key_length),
                                value, client->request.handler_ctx);

        /* check if the callback has closed the value istream */
        valid = memcached_connection_valid(client);
        pool_unref(client->pool);

        return valid;
    } else {
        /* no value: invoke the callback, quit */

        memcached_client_release_socket(client,
                                        fifo_buffer_empty(client->response.input));

        client->response.read_state = READ_END;

        client->request.handler(g_ntohs(client->response.header.status),
                                client->response.extras,
                                client->response.header.extras_length,
                                client->response.key.buffer,
                                g_ntohs(client->response.header.key_length),
                                NULL, client->request.handler_ctx);

        memcached_client_release(client, false);
        return false;
    }
}

/**
 * @return true if at least one byte has been consumed and the stream
 * is still open; false if nothing has been consumed (istream handler
 * blocks), or if an error has occurred, or if the stream is finished
 */
static bool
memcached_consume_value(struct memcached_client *client)
{
    size_t length, nbytes;
    const void *data;

    assert(client->response.read_state == READ_VALUE);
    assert(client->response.remaining > 0);

    data = fifo_buffer_read(client->response.input, &length);
    if (data == NULL)
        return false;

    if (client->fd >= 0 && length >= client->response.remaining)
        memcached_client_release_socket(client,
                                        length == client->response.remaining);

    if (length > client->response.remaining)
        length = client->response.remaining;

    nbytes = istream_invoke_data(&client->response.value, data, length);
    if (nbytes == 0)
        return false;

    fifo_buffer_consume(client->response.input, nbytes);

    client->response.remaining -= nbytes;
    if (client->response.remaining > 0)
        return true;

    assert(client->fd < 0);
    assert(client->request.istream == NULL);

    client->response.read_state = READ_END;
    istream_deinit_eof(&client->response.value);
    pool_unref(client->pool);
    return false;
}

/**
 * @return true if the response is finished, false if more data is
 * needed or if an error has occurred
 */
static bool
memcached_consume_input(struct memcached_client *client)
{
    if (client->response.read_state == READ_HEADER &&
        !memcached_consume_header(client))
        return false;

    if (client->response.read_state == READ_EXTRAS &&
        !memcached_consume_extras(client))
        return false;

    if (client->response.read_state == READ_KEY &&
        !memcached_consume_key(client))
        return false;

    if (client->response.read_state == READ_VALUE &&
        !memcached_consume_value(client))
        return false;

    assert(!fifo_buffer_full(client->response.input));

    return true;
}

/**
 * Read more data from the socket into the input buffer.
 *
 * @return true if at least one byte has been read, false if no data
 * is available or if an error has occurred
 */
static bool
memcached_client_fill_buffer(struct memcached_client *client)
{
    assert(client->fd >= 0);
    assert(client->response.input != NULL);
    assert(!fifo_buffer_full(client->response.input));

    ssize_t nbytes = recv_to_buffer(client->fd, client->response.input,
                                    G_MAXINT);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "memcached server closed the connection\n");
        memcached_connection_close(client);
        return false;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            memcached_client_schedule_read(client);
            return false;
        }

        daemon_log(1, "read error on memcached connection: %s\n",
                   strerror(errno));
        memcached_connection_close(client);
        return false;
    }

    return true;
}

static void
memcached_client_try_read_buffered(struct memcached_client *client)
{
    if (!memcached_client_fill_buffer(client))
        return;

    if (memcached_consume_input(client) && client->fd >= 0)
        memcached_client_schedule_read(client);
}

static void
memcached_client_try_read_direct(struct memcached_client *client)
{
    assert(client->response.read_state == READ_VALUE);
    assert(client->response.remaining > 0);

    if (!fifo_buffer_empty(client->response.input)) {
        if (!memcached_consume_input(client))
            return;

        assert(client->response.remaining > 0);
    }

    ssize_t nbytes = istream_invoke_direct(&client->response.value,
                                           client->fd_type, client->fd,
                                           client->response.remaining);
    if (likely(nbytes > 0)) {
        client->response.remaining -= nbytes;

        if (client->response.remaining == 0) {
            memcached_client_release_socket(client, true);
            istream_deinit_eof(&client->response.value);
            pool_unref(client->pool);
        }
    } else if (unlikely(nbytes == 0)) {
        daemon_log(1, "memcached server closed the connection\n");
        memcached_connection_abort_response_value(client);
    } else if (nbytes == -2 || nbytes == -3) {
        /* either the destination fd blocks (-2) or the stream (and
           the whole connection) has been closed during the direct()
           callback (-3); no further checks */
    } else if (errno == EAGAIN) {
        memcached_client_schedule_read(client);
    } else {
        daemon_log(1, "read error on memcached connection: %s\n",
                   strerror(errno));
        memcached_connection_abort_response_value(client);
    }
}

static void
memcached_client_try_direct(struct memcached_client *client)
{
    assert(client->response.read_state == READ_VALUE);
    assert(client->response.remaining > 0);

    if (!fifo_buffer_empty(client->response.input)) {
        if (!memcached_consume_value(client))
            return;

        pool_unref(client->pool);

        /* at this point, the handler might have changed, and the new
           handler might not support "direct" transfer - check
           again */
        if (!istream_check_direct(&client->response.value, client->fd_type)) {
            memcached_client_schedule_read(client);
            return;
        }
    }

    memcached_client_try_read_direct(client);
}

/**
 * The libevent callback for sending the request to the socket.
 */
static void
memcached_client_send_event_callback(G_GNUC_UNUSED int fd, short event,
                                     void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->fd >= 0);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "memcached_client: send timeout\n");
        memcached_connection_close(client);
        return;
    }

    p_event_consumed(&client->request.event, client->pool);

    istream_read(client->request.istream);

    pool_commit();
}

/**
 * The libevent callback for receiving the response from the socket.
 */
static void
memcached_client_recv_event_callback(G_GNUC_UNUSED int fd, short event,
                                     void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->fd >= 0);

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "memcached_client: receive timeout\n");
        memcached_connection_close(client);
        return;
    }

    p_event_consumed(&client->response.event, client->pool);

    if (client->response.read_state == READ_VALUE &&
        istream_check_direct(&client->response.value, client->fd_type))
        memcached_client_try_direct(client);
    else
        memcached_client_try_read_buffered(client);

    pool_commit();
}

/*
 * istream handler for the request
 *
 */

static size_t
memcached_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct memcached_client *client = ctx;
    ssize_t nbytes;

    assert(client->fd >= 0);
    assert(client->request.istream != NULL);
    assert(client->response.read_state == READ_HEADER ||
           client->response.read_state == READ_EXTRAS ||
           client->response.read_state == READ_KEY);
    assert(data != NULL);
    assert(length > 0);

    nbytes = send(client->fd, data, length, MSG_DONTWAIT|MSG_NOSIGNAL);
    if (nbytes < 0) {
        if (errno == EAGAIN) {
            memcached_client_schedule_write(client);
            return 0;
        }

        daemon_log(1, "write error on memcached connection: %s\n",
                   strerror(errno));
        memcached_connection_close(client);
        return 0;
    }

    memcached_client_schedule_write(client);
    return (size_t)nbytes;
}

static void
memcached_request_stream_eof(void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->request.istream != NULL);
    assert(client->response.read_state == READ_HEADER ||
           client->response.read_state == READ_EXTRAS ||
           client->response.read_state == READ_KEY);

    client->request.istream = NULL;

    p_event_del(&client->request.event, client->pool);
    memcached_client_schedule_read(client);
}

static void
memcached_request_stream_abort(void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->request.istream != NULL);
    assert(client->response.read_state == READ_HEADER ||
           client->response.read_state == READ_EXTRAS ||
           client->response.read_state == READ_KEY);

    client->request.istream = NULL;

    memcached_connection_close(client);
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
    return (struct memcached_client*)(((char*)ao) - offsetof(struct memcached_client, request.async));
}

static void
memcached_client_request_abort(struct async_operation *ao)
{
    struct memcached_client *client
        = async_to_memcached_client(ao);

    /* async_abort() can only be used before the response was
       delivered to our callback */
    assert(client->response.read_state == READ_HEADER ||
           client->response.read_state == READ_EXTRAS ||
           client->response.read_state == READ_KEY);

    memcached_connection_abort_response_header(client);
}

static const struct async_operation_class memcached_client_async_operation = {
    .abort = memcached_client_request_abort,
};

/*
 * constructor
 *
 */

void
memcached_client_invoke(pool_t pool, int fd, enum istream_direct fd_type,
                        const struct lease *lease, void *lease_ctx,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        istream_t value,
                        memcached_response_handler_t handler, void *handler_ctx,
                        struct async_operation_ref *async_ref)
{
    struct memcached_client *client;
    istream_t request;

    assert(extras_length <= MEMCACHED_EXTRAS_MAX);
    assert(key_length <= MEMCACHED_KEY_MAX);

    request = memcached_request_packet(pool, opcode, extras, extras_length,
                                       key, key_length, value,
                                       0x1234 /* XXX? */);
    if (request == NULL) {
        lease_direct_release(lease, lease_ctx, true);
        handler(-1, NULL, 0, NULL, 0, NULL, handler_ctx);
        return;
    }

    pool_ref(pool);
    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->fd = fd;
    client->fd_type = fd_type;
    p_lease_ref_set(&client->lease_ref, lease, lease_ctx,
                    pool, "memcached_client_lease");

    event_set(&client->request.event, client->fd, EV_WRITE|EV_TIMEOUT,
              memcached_client_send_event_callback, client);
    event_set(&client->response.event, client->fd, EV_READ|EV_TIMEOUT,
              memcached_client_recv_event_callback, client);

    istream_assign_handler(&client->request.istream, request,
                           &memcached_request_stream_handler, client,
                           0);

    client->request.handler = handler;
    client->request.handler_ctx = handler_ctx;

    async_init(&client->request.async, &memcached_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    client->response.read_state = READ_HEADER;
    client->response.input = fifo_buffer_new(client->pool, 8192);

    istream_read(client->request.istream);
}
