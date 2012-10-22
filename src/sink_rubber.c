/*
 * An istream sink that copies data into a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_rubber.h"
#include "istream-internal.h"
#include "async.h"
#include "rubber.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct sink_rubber {
    struct istream *input;

    struct rubber *rubber;
    unsigned rubber_id;

    size_t max_size, position;

    const struct sink_rubber_handler *handler;
    void *handler_ctx;

    struct async_operation async_operation;
};

static ssize_t
fd_read(istream_direct_t type, int fd, void *p, size_t size)
{
    return type == ISTREAM_SOCKET || type == ISTREAM_TCP
        ? recv(fd, p, size, MSG_DONTWAIT)
        : read(fd, p, size);
}

static void
sink_rubber_abort_too_large(struct sink_rubber *s)
{
    rubber_remove(s->rubber, s->rubber_id);
    async_operation_finished(&s->async_operation);

    if (s->input != NULL)
        istream_free_handler(&s->input);

    s->handler->too_large(s->handler_ctx);
}

static void
sink_rubber_eof(struct sink_rubber *s)
{
    async_operation_finished(&s->async_operation);

    if (s->input != NULL)
        istream_free_handler(&s->input);

    rubber_shrink(s->rubber, s->rubber_id, s->position);
    s->handler->done(s->rubber_id, s->position, s->handler_ctx);
}

/*
 * istream handler
 *
 */

static size_t
sink_rubber_input_data(const void *data, size_t length, void *ctx)
{
    struct sink_rubber *s = ctx;
    assert(s->position <= s->max_size);

    if (s->position + length > s->max_size) {
        /* too large, abort and invoke handler */

        sink_rubber_abort_too_large(s);
        return 0;
    }

    uint8_t *p = rubber_write(s->rubber, s->rubber_id);
    memcpy(p + s->position, data, length);
    s->position += length;

    return length;
}

static ssize_t
sink_rubber_input_direct(istream_direct_t type, int fd,
                         size_t max_length, void *ctx)
{
    struct sink_rubber *s = ctx;
    assert(s->position <= s->max_size);

    size_t length = s->max_size - s->position;
    if (length == 0) {
        /* already full, see what the file descriptor says */

        uint8_t dummy;
        ssize_t nbytes = fd_read(type, fd, &dummy, sizeof(dummy));
        if (nbytes > 0) {
            sink_rubber_abort_too_large(s);
            return ISTREAM_RESULT_CLOSED;
        }

        if (nbytes == 0) {
            sink_rubber_eof(s);
            return ISTREAM_RESULT_CLOSED;
        }

        return ISTREAM_RESULT_ERRNO;
    }

    if (length > max_length)
        length = max_length;

    uint8_t *p = rubber_write(s->rubber, s->rubber_id);
    p += s->position;

    ssize_t nbytes = fd_read(type, fd, p, length);
    if (nbytes > 0)
        s->position += (size_t)nbytes;

    return nbytes;
}

static void
sink_rubber_input_eof(void *ctx)
{
    struct sink_rubber *s = ctx;

    assert(s->input != NULL);
    s->input = NULL;

    sink_rubber_eof(s);
}

static void
sink_rubber_input_abort(GError *error, void *ctx)
{
    struct sink_rubber *s = ctx;

    assert(s->input != NULL);
    s->input = NULL;

    rubber_remove(s->rubber, s->rubber_id);
    async_operation_finished(&s->async_operation);
    s->handler->error(error, s->handler_ctx);
}

static const struct istream_handler sink_rubber_input_handler = {
    .data = sink_rubber_input_data,
    .direct = sink_rubber_input_direct,
    .eof = sink_rubber_input_eof,
    .abort = sink_rubber_input_abort,
};


/*
 * async operation
 *
 */

static struct sink_rubber *
async_to_sink_rubber(struct async_operation *ao)
{
    return (struct sink_rubber*)(((char*)ao) - offsetof(struct sink_rubber, async_operation));
}

static void
sink_rubber_abort(struct async_operation *ao)
{
    struct sink_rubber *s = async_to_sink_rubber(ao);

    rubber_remove(s->rubber, s->rubber_id);

    if (s->input != NULL)
        istream_free_handler(&s->input);
}

static const struct async_operation_class sink_rubber_operation = {
    .abort = sink_rubber_abort,
};


/*
 * constructor
 *
 */

void
sink_rubber_new(struct pool *pool, struct istream *input,
                struct rubber *rubber, size_t max_size,
                const struct sink_rubber_handler *handler, void *ctx,
                struct async_operation_ref *async_ref)
{
    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(handler != NULL);
    assert(handler->done != NULL);
    assert(handler->out_of_memory != NULL);
    assert(handler->too_large != NULL);
    assert(handler->error != NULL);

    const off_t available = istream_available(input, true);
    if (available > (off_t)max_size) {
        istream_close_unused(input);
        handler->too_large(ctx);
        return;
    }

    const off_t size = istream_available(input, false);
    assert(size == -1 || size >= available);
    assert(size <= (off_t)max_size);
    if (size == 0) {
        istream_close_unused(input);
        handler->done(0, 0, ctx);
        return;
    }

    const size_t allocate = size == -1
        ? max_size
        : (size_t)size;

    unsigned rubber_id = rubber_add(rubber, allocate);
    if (rubber_id == 0) {
        istream_close_unused(input);
        handler->out_of_memory(ctx);
        return;
    }

    struct sink_rubber *s = p_malloc(pool, sizeof(*s));
    s->rubber = rubber;
    s->rubber_id = rubber_id;
    s->max_size = allocate;
    s->position = 0;
    s->handler = handler;
    s->handler_ctx = ctx;

    istream_assign_handler(&s->input, input,
                           &sink_rubber_input_handler, s,
                           ISTREAM_ANY);

    async_init(&s->async_operation, &sink_rubber_operation);
    async_ref_set(async_ref, &s->async_operation);
}
