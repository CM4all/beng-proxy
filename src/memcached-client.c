/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "memcached-client.h"
#include "memcached-packet.h"
#include "lease.h"
#include "async.h"
#include "event2.h"
#include "istream-internal.h"
#include "buffered-io.h"

#include <daemon/log.h>

#include <glib.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>

struct memcached_client {
    pool_t pool;

    /* I/O */
    int fd;
    struct lease_ref lease_ref;
    struct event2 event;

    /* request */
    struct {
        memcached_response_handler_t handler;
        void *handler_ctx;

        struct async_operation async;

        istream_t istream;
    } request;

    /* response */
    struct {
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
        size_t value_remaining;
    } response;
};

static const struct timeval timeout = {
    .tv_sec = 5,
    .tv_usec = 0,
};

static inline bool
memcached_connection_valid(const struct memcached_client *client)
{
    return client->fd >= 0;
}

/**
 * Release resources held by this object: the event object, the socket
 * lease, and the pool reference.
 */
static void
memcached_client_release(struct memcached_client *client, bool reuse)
{
    assert(client != NULL);

    event2_set(&client->event, 0);
    event2_commit(&client->event);
    client->fd = -1;
    lease_release(&client->lease_ref, reuse);
    pool_unref(client->pool);
}

static void
memcached_connection_close(struct memcached_client *client)
{
    if (!memcached_connection_valid(client))
        return;

    pool_ref(client->pool);

    event2_set(&client->event, 0);
    event2_commit(&client->event);

    switch (client->response.read_state) {
    case READ_HEADER:
    case READ_EXTRAS:
    case READ_KEY:
        client->request.handler(-1, NULL, 0, NULL, 0, NULL,
                                client->request.handler_ctx);
        client->response.read_state = READ_END;
        break;

    case READ_VALUE:
        istream_deinit_abort(&client->response.value);
        client->response.read_state = READ_END;
        break;

    case READ_END:
        break;
    }

    if (client->request.istream != NULL)
        istream_free_handler(&client->request.istream);

    if (client->fd >= 0)
        memcached_client_release(client, false);

    pool_unref(client->pool);
}

static bool
memcached_consume_input(struct memcached_client *client);

static void
memcached_client_try_read(struct memcached_client *client);

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

    return client->response.value_remaining;
}

static void
istream_memcached_read(istream_t istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == READ_VALUE);

    if (!fifo_buffer_empty(client->response.input))
        memcached_consume_input(client);
    else {
        pool_ref(client->pool);
        memcached_client_try_read(client);
        pool_unref(client->pool);
    }
}

static void
istream_memcached_close(istream_t istream)
{
    struct memcached_client *client = istream_to_memcached_client(istream);

    assert(client->response.read_state == READ_VALUE);

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

static bool
memcached_consume_header(struct memcached_client *client)
{
    size_t length;
    const void *data = fifo_buffer_read(client->response.input, &length);

    assert(client->response.read_state == READ_HEADER);

    if (data == NULL || length < sizeof(client->response.header))
        /* not enough data yet */
        return false;

    memcpy(&client->response.header, data, sizeof(client->response.header));
    fifo_buffer_consume(client->response.input,
                        sizeof(client->response.header));
    client->response.read_state = READ_EXTRAS;

    client->response.value_remaining =
        g_ntohl(client->response.header.body_length) -
        g_ntohs(client->response.header.key_length) -
        client->response.header.extras_length;
    if (client->response.header.magic != MEMCACHED_MAGIC_RESPONSE ||
        client->response.value_remaining > G_MAXINT) {
        /* integer underflow */
        memcached_connection_close(client);
        return false;
    }

    return true;
}

static bool
memcached_consume_extras(struct memcached_client *client)
{
    assert(client->response.read_state == READ_EXTRAS);

    if (client->response.header.extras_length > 0) {
        size_t length;
        const void *data = fifo_buffer_read(client->response.input, &length);

        if (data == NULL ||
            length < sizeof(client->response.header.extras_length))
            return false;

        client->response.extras = p_malloc(client->pool,
                                           client->response.header.extras_length);
        memcpy(client->response.extras, data,
               client->response.header.extras_length);

        fifo_buffer_consume(client->response.input,
                            client->response.header.extras_length);
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

static bool
memcached_consume_key(struct memcached_client *client)
{
    assert(client->response.read_state == READ_KEY);

    if (client->response.key.remaining > 0) {
        size_t length;
        const void *data = fifo_buffer_read(client->response.input, &length);

        if (data == NULL)
            return false;

        if (length > client->response.key.remaining)
            length = client->response.key.remaining;

        memcpy(client->response.key.tail, data, length);
        client->response.key.tail += length;
        client->response.key.remaining -= length;

        if (client->response.key.remaining > 0)
            return false;
    }

    if (client->response.value_remaining > 0) {
        /* there's a value: pass it to the callback, continue
           reading */
        istream_t value;
        bool valid;

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
        client->response.read_state = READ_END;

        client->request.handler(g_ntohs(client->response.header.status),
                                client->response.extras,
                                client->response.header.extras_length,
                                client->response.key.buffer,
                                g_ntohs(client->response.header.key_length),
                                NULL, client->request.handler_ctx);

        memcached_connection_close(client);
        return false;
    }
}

static bool
memcached_consume_value(struct memcached_client *client)
{
    size_t length, nbytes;
    const void *data;

    assert(client->response.read_state == READ_VALUE);
    assert(client->response.value_remaining > 0);

    data = fifo_buffer_read(client->response.input, &length);
    if (data == NULL)
        return false;

    if (length > client->response.value_remaining)
        length = client->response.value_remaining;

    nbytes = istream_invoke_data(&client->response.value, data, length);
    if (nbytes == 0)
        return false;

    fifo_buffer_consume(client->response.input, nbytes);

    client->response.value_remaining -= nbytes;
    if (client->response.value_remaining > 0)
        return false;

    client->response.read_state = READ_END;
    istream_deinit_eof(&client->response.value);
    memcached_connection_close(client);
    return true;
}

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

    assert(client->response.read_state == READ_VALUE);

    return memcached_consume_value(client);
}

static void
memcached_client_try_read(struct memcached_client *client)
{
    ssize_t nbytes;

    assert(memcached_connection_valid(client));

    nbytes = read_to_buffer(client->fd, client->response.input,
                            G_MAXINT);
    assert(nbytes != -2);

    if (nbytes == 0) {
        daemon_log(1, "memcached server closed the connection\n");
        memcached_connection_close(client);
        return;
    }

    if (nbytes < 0) {
        if (errno == EAGAIN) {
            event2_or(&client->event, EV_READ);
            return;
        }

        daemon_log(1, "read error on memcached connection: %s\n",
                   strerror(errno));
        memcached_connection_close(client);
        return;
    }

    memcached_consume_input(client);

    if (memcached_connection_valid(client) &&
        !fifo_buffer_full(client->response.input))
        event2_setbit(&client->event, EV_READ,
                      !fifo_buffer_full(client->response.input));
}

/**
 * The libevent callback for the socket.
 */
static void
memcached_client_event_callback(G_GNUC_UNUSED int fd, short event, void *ctx)
{
    struct memcached_client *client = ctx;

    if (unlikely(event & EV_TIMEOUT)) {
        daemon_log(4, "timeout\n");
        memcached_connection_close(client);
        return;
    }

    pool_ref(client->pool);

    event2_reset(&client->event);
    event2_lock(&client->event);
    event2_or(&client->event, EV_READ);

    if (memcached_connection_valid(client) && (event & EV_WRITE) != 0)
        istream_read(client->request.istream);

    if (memcached_connection_valid(client) && (event & EV_READ) != 0)
        memcached_client_try_read(client);

    event2_unlock(&client->event);

    pool_unref(client->pool);
    pool_commit();
}

/*
 * request generator
 *
 */

static size_t
memcached_write(struct memcached_client *client, const void *data, size_t length)
{
    ssize_t nbytes;

    assert(client != NULL);
    assert(client->fd >= 0);
    assert(data != NULL);
    assert(length > 0);

    nbytes = write(client->fd, data, length);
    if (nbytes < 0) {
        daemon_log(1, "write error on memcached connection: %s\n",
                   strerror(errno));
        memcached_connection_close(client);
        return 0;
    }

    event2_or(&client->event, EV_WRITE);
    return (size_t)nbytes;
}

/*
 * istream handler for the request
 *
 */

static size_t
memcached_request_stream_data(const void *data, size_t length, void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->fd >= 0);
    assert(client->request.istream != NULL);

    return memcached_write(client, data, length);
}

static void
memcached_request_stream_eof(void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->request.istream != NULL);

    client->request.istream = NULL;

    event2_nand(&client->event, EV_WRITE);
}

static void
memcached_request_stream_abort(void *ctx)
{
    struct memcached_client *client = ctx;

    assert(client->request.istream != NULL);

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
    assert(client->response.read_state == READ_HEADER);

    /* by setting the state to READ_END, we bar
       memcached_client_request_close() from invoking the "abort"
       callback */
    client->response.read_state = READ_END;

    memcached_connection_close(client);
}

static const struct async_operation_class memcached_client_async_operation = {
    .abort = memcached_client_request_abort,
};

/*
 * constructor
 *
 */

void
memcached_client_invoke(pool_t pool, int fd,
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

    assert(extras_length <= 0xff);
    assert(key_length < 0x1000);

    request = memcached_request_packet(pool, opcode, extras, extras_length,
                                       key, key_length, value,
                                       0x1234 /* XXX? */);
    if (request == NULL) {
        struct lease_ref lease_ref;
        lease_ref_set(&lease_ref, lease, lease_ctx);
        lease_release(&lease_ref, true);

        handler(-1, NULL, 0, NULL, 0, NULL, handler_ctx);
        return;
    }

    pool_ref(pool);
    client = p_malloc(pool, sizeof(*client));
    client->pool = pool;
    client->fd = fd;
    lease_ref_set(&client->lease_ref, lease, lease_ctx);
    event2_init(&client->event, fd,
                memcached_client_event_callback, client, &timeout);

    istream_assign_handler(&client->request.istream, request,
                           &memcached_request_stream_handler, client,
                           0);

    client->request.handler = handler;
    client->request.handler_ctx = handler_ctx;

    async_init(&client->request.async, &memcached_client_async_operation);
    async_ref_set(async_ref, &client->request.async);

    client->response.read_state = READ_HEADER;
    client->response.input = fifo_buffer_new(client->pool, 8192);

    pool_ref(client->pool);

    event2_lock(&client->event);
    event2_set(&client->event, EV_READ);

    istream_read(client->request.istream);

    event2_unlock(&client->event);

    pool_unref(client->pool);
}
