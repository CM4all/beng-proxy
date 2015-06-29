#include "sink_buffer.hxx"
#include "istream.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct BufferSink {
    struct pool *pool;
    struct istream *input;

    unsigned char *buffer;
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
    auto *buffer = (BufferSink *)ctx;

    assert(buffer->position < buffer->size);
    assert(length <= buffer->size - buffer->position);

    memcpy(buffer->buffer + buffer->position, data, length);
    buffer->position += length;

    return length;
}

static ssize_t
sink_buffer_input_direct(gcc_unused FdType type, int fd,
                         size_t max_length, void *ctx)
{
    auto *buffer = (BufferSink *)ctx;

    size_t length = buffer->size - buffer->position;
    if (length > max_length)
        length = max_length;

    ssize_t nbytes = type == FdType::FD_SOCKET || type == FdType::FD_TCP
        ? recv(fd, buffer->buffer + buffer->position, length, MSG_DONTWAIT)
        : read(fd, buffer->buffer + buffer->position, length);
    if (nbytes > 0)
        buffer->position += (size_t)nbytes;

    return nbytes;
}

static void
sink_buffer_input_eof(void *ctx)
{
    auto *buffer = (BufferSink *)ctx;

    assert(buffer->position == buffer->size);

    buffer->async_operation.Finished();
    buffer->handler->done(buffer->buffer, buffer->size, buffer->handler_ctx);
}

static void
sink_buffer_input_abort(GError *error, void *ctx)
{
    auto *buffer = (BufferSink *)ctx;

    buffer->async_operation.Finished();
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

static BufferSink *
async_to_sink_buffer(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &BufferSink::async_operation);
}

static void
sink_buffer_abort(struct async_operation *ao)
{
    BufferSink *buffer = async_to_sink_buffer(ao);

    const ScopePoolRef ref(*buffer->pool TRACE_ARGS);
    istream_close_handler(buffer->input);
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

    auto buffer = NewFromPool<BufferSink>(*pool);
    buffer->pool = pool;

    istream_assign_handler(&buffer->input, input,
                           &sink_buffer_input_handler, buffer,
                           FD_ANY);

    buffer->size = (size_t)available;
    buffer->position = 0;
    buffer->buffer = (unsigned char *)p_malloc(pool, buffer->size);
    buffer->handler = handler;
    buffer->handler_ctx = ctx;

    buffer->async_operation.Init(sink_buffer_operation);
    async_ref->Set(buffer->async_operation);
}
