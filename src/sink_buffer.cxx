#include "sink_buffer.hxx"
#include "istream-internal.h"
#include "async.h"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct sink_buffer {
    struct pool *pool;
    struct istream *input;

    unsigned char *data;
    size_t size, position;

    const struct sink_buffer_handler *handler;
    void *handler_ctx;

    struct async_operation async_operation;
};

static GQuark
sink_buffer_quark(void)
{
    return g_quark_from_static_string("sink_buffer");
}

/*
 * istream handler
 *
 */

static size_t
sink_buffer_input_data(const void *data, size_t length, void *ctx)
{
    sink_buffer *buffer = (sink_buffer *)ctx;

    assert(buffer->position < buffer->size);
    assert(length <= buffer->size - buffer->position);

    memcpy(buffer->data + buffer->position, data, length);
    buffer->position += length;

    return length;
}

static ssize_t
sink_buffer_input_direct(gcc_unused istream_direct type, int fd,
                         size_t max_length, void *ctx)
{
    sink_buffer *buffer = (sink_buffer *)ctx;
    size_t length = buffer->size - buffer->position;
    ssize_t nbytes;

    if (length > max_length)
        length = max_length;

    nbytes = type == ISTREAM_SOCKET || type == ISTREAM_TCP
        ? recv(fd, buffer->data + buffer->position, length, MSG_DONTWAIT)
        : read(fd, buffer->data + buffer->position, length);
    if (nbytes > 0)
        buffer->position += (size_t)nbytes;

    return nbytes;
}

static void
sink_buffer_input_eof(void *ctx)
{
    sink_buffer *buffer = (sink_buffer *)ctx;

    assert(buffer->position == buffer->size);

    async_operation_finished(&buffer->async_operation);
    buffer->handler->done(buffer->data, buffer->size, buffer->handler_ctx);
}

static void
sink_buffer_input_abort(GError *error, void *ctx)
{
    sink_buffer *buffer = (sink_buffer *)ctx;

    async_operation_finished(&buffer->async_operation);
    buffer->handler->error(error, buffer->handler_ctx);
}

static const struct istream_handler sink_buffer_input_handler = {
    .data = sink_buffer_input_data,
    .direct = sink_buffer_input_direct,
    .eof = sink_buffer_input_eof,
    .abort = sink_buffer_input_abort,
};


/*
 * async operation
 *
 */

static struct sink_buffer *
async_to_sink_buffer(struct async_operation *ao)
{
    void *p = ((char *)ao) - offsetof(struct sink_buffer, async_operation);
    return (struct sink_buffer *)p;
}

static void
sink_buffer_abort(struct async_operation *ao)
{
    struct sink_buffer *buffer = async_to_sink_buffer(ao);

    pool_ref(buffer->pool);
    istream_close_handler(buffer->input);
    pool_unref(buffer->pool);
}

static const struct async_operation_class sink_buffer_operation = {
    .abort = sink_buffer_abort,
};


/*
 * constructor
 *
 */

void
sink_buffer_new(struct pool *pool, struct istream *input,
                const struct sink_buffer_handler *handler, void *ctx,
                struct async_operation_ref *async_ref)
{
    off_t available;
    static char empty_buffer[1];

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(handler != NULL);
    assert(handler->done != NULL);
    assert(handler->error != NULL);

    available = istream_available(input, false);
    if (available == -1 || available >= 0x10000000) {
        istream_close_unused(input);

        GError *error =
            g_error_new_literal(sink_buffer_quark(), 0,
                                available < 0
                                ? "unknown stream length"
                                : "stream is too large");
        handler->error(error, ctx);
        return;
    }

    if (available == 0) {
        istream_close_unused(input);
        handler->done(empty_buffer, 0, ctx);
        return;
    }

    auto buffer = PoolAlloc<sink_buffer>(pool);
    buffer->pool = pool;

    istream_assign_handler(&buffer->input, input,
                           &sink_buffer_input_handler, buffer,
                           ISTREAM_ANY);

    buffer->size = (size_t)available;
    buffer->position = 0;
    buffer->data = (unsigned char *)p_malloc(pool, buffer->size);
    buffer->handler = handler;
    buffer->handler_ctx = ctx;

    async_init(&buffer->async_operation, &sink_buffer_operation);
    async_ref_set(async_ref, &buffer->async_operation);
}
