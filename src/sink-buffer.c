/*
 * This istream filter reads a 32 bit header size from the stream,
 * reads it into a buffer and invokes a callback with the tail of the
 * stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink-impl.h"
#include "istream-internal.h"
#include "async.h"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <unistd.h>

struct sink_buffer {
    pool_t pool;
    istream_t input;

    unsigned char *data;
    size_t size, position;

    void (*callback)(void *data, size_t length, void *ctx);
    void *callback_ctx;

    struct async_operation async_operation;
};


/*
 * istream handler
 *
 */

static size_t
sink_buffer_input_data(const void *data, size_t length, void *ctx)
{
    struct sink_buffer *buffer = ctx;

    assert(buffer->position < buffer->size);
    assert(length <= buffer->size - buffer->position);

    memcpy(buffer->data + buffer->position, data, length);
    buffer->position += length;

    return length;
}

static ssize_t
sink_buffer_input_direct(G_GNUC_UNUSED istream_direct_t type, int fd,
                         size_t max_length, void *ctx)
{
    struct sink_buffer *buffer = ctx;
    size_t length = buffer->size - buffer->position;
    ssize_t nbytes;

    if (length > max_length)
        length = max_length;

    nbytes = read(fd, buffer->data + buffer->position, length);
    if (nbytes > 0)
        buffer->position += (size_t)nbytes;

    return nbytes;
}

static void
sink_buffer_input_eof(void *ctx)
{
    struct sink_buffer *buffer = ctx;

    assert(buffer->position == buffer->size);

    buffer->callback(buffer->data, buffer->size, buffer->callback_ctx);

}

static void
sink_buffer_input_abort(void *ctx)
{
    struct sink_buffer *buffer = ctx;

    buffer->callback(NULL, 0, buffer->callback_ctx);
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
    return (struct sink_buffer*)(((char*)ao) - offsetof(struct sink_buffer, async_operation));
}

static void
sink_buffer_abort(struct async_operation *ao)
{
    struct sink_buffer *buffer = async_to_sink_buffer(ao);

    pool_ref(buffer->pool);
    istream_close_handler(buffer->input);
    buffer->callback(NULL, 0, buffer->callback_ctx);
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
sink_buffer_new(pool_t pool, istream_t input,
                void (*callback)(void *data, size_t length, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref)
{
    off_t available;
    struct sink_buffer *buffer;
    static char empty_buffer[1];

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(callback != NULL);

    available = istream_available(input, false);
    if (available == -1 || available >= 0x10000000) {
        callback(NULL, 0, ctx);
        return;
    }

    if (available == 0) {
        callback(empty_buffer, 0, ctx);
        return;
    }

    buffer = p_malloc(pool, sizeof(*buffer));
    buffer->pool = pool;

    istream_assign_handler(&buffer->input, input,
                           &sink_buffer_input_handler, buffer,
                           ISTREAM_ANY);

    buffer->size = (size_t)available;
    buffer->position = 0;
    buffer->data = p_malloc(pool, buffer->size);
    buffer->callback = callback;
    buffer->callback_ctx = ctx;

    async_init(&buffer->async_operation, &sink_buffer_operation);
    async_ref_set(async_ref, &buffer->async_operation);
}
