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
#include <stdint.h>

struct sink_header {
    struct istream output;

    enum {
        SIZE, HEADER, CALLBACK, DATA
    } state;

    istream_t input;

    unsigned char size_buffer[4];

    unsigned char *buffer;
    size_t size, position;

    /**
     * How much data of the input is pending to be consumed?  Only
     * valid while state==CALLBACK.
     */
    size_t pending;

    void (*callback)(void *header, size_t length,
                     istream_t tail, void *ctx);
    void *callback_ctx;

    struct async_operation async_operation;
};

static size_t
header_invoke_callback(struct sink_header *header, size_t consumed)
{
    assert(header->state == SIZE || header->state == HEADER);

    pool_ref(header->output.pool);

    /* the base value has been set by sink_header_input_data() */
    header->pending += consumed;

    header->state = CALLBACK;
    header->callback(header->buffer, header->size,
                     istream_struct_cast(&header->output),
                     header->callback_ctx);

    if (header->input != NULL) {
        header->state = DATA;
        istream_handler_set_direct(header->input,
                                   header->output.handler_direct);
    } else
        /* we have been closed meanwhile; bail out */
        consumed = 0;

    pool_unref(header->output.pool);

    return consumed;
}

static size_t
header_consume_size(struct sink_header *header,
                    const void *data, size_t length)
{
    assert(header->position < sizeof(header->size_buffer));

    if (length > sizeof(header->size_buffer) - header->position)
        length = sizeof(header->size_buffer) - header->position;

    memcpy(header->size_buffer + header->position, data, length);
    header->position += length;

    if (header->position < sizeof(header->size_buffer))
        return length;

    header->size = g_ntohl(*(const uint32_t *)header->size_buffer);
    if (header->size > 0x100000) {
        /* header too large */
        istream_close_handler(header->input);
        header->callback(NULL, 0, NULL, header->callback_ctx);
        istream_deinit(&header->output);
        return 0;
    }

    if (header->size > 0) {
        header->buffer = p_malloc(header->output.pool, header->size);
        header->state = HEADER;
        header->position = 0;
    } else {
        /* header empty: don't allocate, invoke callback now */

        header->buffer = NULL;

        length = header_invoke_callback(header, length);
    }

    return length;
}

static size_t
header_consume_header(struct sink_header *header,
                      const void *data, size_t length)
{
    size_t nbytes = header->size - header->position;

    assert(header->position < header->size);

    if (nbytes > length)
        nbytes = length;

    memcpy(header->buffer + header->position, data, nbytes);
    header->position += nbytes;

    if (header->position < header->size)
        return nbytes;

    return header_invoke_callback(header, nbytes);
}


/*
 * istream handler
 *
 */

static size_t
sink_header_input_data(const void *data0, size_t length, void *ctx)
{
    struct sink_header *header = ctx;
    const unsigned char *data = data0;
    size_t consumed = 0, nbytes;

    if (header->state == DATA)
        return istream_invoke_data(&header->output, data, length);

    if (header->state == SIZE) {
        header->pending = 0; /* just in case the callback is invoked */

        consumed = header_consume_size(header, data, length);
        if (consumed == 0)
            return 0;

        if (consumed == length)
            return length;

        data += consumed;
        length -= consumed;
    }

    if (header->state == HEADER) {
        header->pending = consumed; /* just in case the callback is invoked */

        nbytes = header_consume_header(header, data, length);
        if (nbytes == 0)
            return 0;

        consumed += nbytes;
        if (consumed == length)
            return length;

        data += nbytes;
        length -= nbytes;
    }

    assert(consumed > 0);

    if (header->state == DATA) {
        pool_ref(header->output.pool);

        nbytes = istream_invoke_data(&header->output, data, length);
        if (nbytes == 0 && header->input == NULL)
            consumed = 0;
        else
            consumed += nbytes;

        pool_unref(header->output.pool);
    }

    return consumed;
}

static ssize_t
sink_header_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct sink_header *header = ctx;

    assert(header->state == DATA);

    return istream_invoke_direct(&header->output, type, fd, max_length);
}

static void
sink_header_input_eof(void *ctx)
{
    struct sink_header *header = ctx;

    switch (header->state) {
    case SIZE:
    case HEADER:
        header->callback(NULL, 0, NULL, header->callback_ctx);
        istream_deinit(&header->output);
        break;

    case CALLBACK:
        assert(false);
        break;

    case DATA:
        istream_deinit_eof(&header->output);
        break;
    }
}

static void
sink_header_input_abort(void *ctx)
{
    struct sink_header *header = ctx;

    switch (header->state) {
    case SIZE:
    case HEADER:
        header->callback(NULL, 0, NULL, header->callback_ctx);
        istream_deinit(&header->output);
        break;

    case CALLBACK:
        assert(false);
        break;

    case DATA:
        istream_deinit_abort(&header->output);
        break;
    }
}

static const struct istream_handler sink_header_input_handler = {
    .data = sink_header_input_data,
    .direct = sink_header_input_direct,
    .eof = sink_header_input_eof,
    .abort = sink_header_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct sink_header *
istream_to_header(istream_t istream)
{
    return (struct sink_header *)(((char*)istream) - offsetof(struct sink_header, output));
}

static off_t
sink_header_available(istream_t istream, bool partial)
{
    struct sink_header *header = istream_to_header(istream);
    off_t available = istream_available(header->input, partial);

    if (available >= 0 && header->state == CALLBACK) {
        if (available < (off_t)header->pending) {
            assert(partial);

            return -1;
        }

        available -= header->pending;
    }

    return available;
}

static void
sink_header_read(istream_t istream)
{
    struct sink_header *header = istream_to_header(istream);

    if (header->state == CALLBACK)
        /* workaround: when invoking the callback from the data()
           handler, it would be illegal to call header->input again */
        return;

    istream_handler_set_direct(header->input, header->output.handler_direct);
    istream_read(header->input);
}

static void
sink_header_close(istream_t istream)
{
    struct sink_header *header = istream_to_header(istream);

    istream_free_handler(&header->input);
    istream_deinit_abort(&header->output);
}

static const struct istream istream_sink = {
    .available = sink_header_available,
    .read = sink_header_read,
    .close = sink_header_close,
};


/*
 * async operation
 *
 */

static struct sink_header *
async_to_sink_header(struct async_operation *ao)
{
    return (struct sink_header*)(((char*)ao) - offsetof(struct sink_header, async_operation));
}

static void
sink_header_abort(struct async_operation *ao)
{
    struct sink_header *header = async_to_sink_header(ao);

    istream_close_handler(header->input);
    istream_deinit(&header->output);
}

static const struct async_operation_class sink_header_operation = {
    .abort = sink_header_abort,
};


/*
 * constructor
 *
 */

void
sink_header_new(pool_t pool, istream_t input,
                void (*callback)(void *header, size_t length,
                                 istream_t tail, void *ctx),
                void *ctx,
                struct async_operation_ref *async_ref)
{
    struct sink_header *header = (struct sink_header *)
        istream_new(pool, &istream_sink, sizeof(*header));

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(callback != NULL);

    istream_assign_handler(&header->input, input,
                           &sink_header_input_handler, header,
                           0);

    header->state = SIZE;
    header->position = 0;
    header->callback = callback;
    header->callback_ctx = ctx;

    async_init(&header->async_operation, &sink_header_operation);
    async_ref_set(async_ref, &header->async_operation);
}
