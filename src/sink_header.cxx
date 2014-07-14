/*
 * This istream filter reads a 32 bit header size from the stream,
 * reads it into a buffer and invokes a callback with the tail of the
 * stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_header.hxx"
#include "istream-internal.h"
#include "async.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <stdint.h>

struct sink_header {
    struct istream output;

    enum {
        SIZE, HEADER, CALLBACK, DATA
    } state;

    struct istream *input;

    unsigned char size_buffer[4];

    unsigned char *buffer;
    size_t size, position;

    /**
     * How much data of the input is pending to be consumed?  Only
     * valid while state==CALLBACK.
     */
    size_t pending;

    const struct sink_header_handler *handler;
    void *handler_ctx;

    struct async_operation async_operation;
};

static GQuark
sink_header_quark(void)
{
    return g_quark_from_static_string("sink_header");
}

static size_t
header_invoke_callback(struct sink_header *header, size_t consumed)
{
    assert(header->state == sink_header::SIZE ||
           header->state == sink_header::HEADER);

    header->async_operation.Finished();

    pool_ref(header->output.pool);

    /* the base value has been set by sink_header_input_data() */
    header->pending += consumed;

    header->state = sink_header::CALLBACK;
    header->handler->done(header->buffer, header->size,
                          istream_struct_cast(&header->output),
                          header->handler_ctx);

    if (header->input != NULL) {
        header->state = sink_header::DATA;
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

    const void *size_buffer = header->size_buffer;
        const uint32_t *size_p = (const uint32_t *)size_buffer;
    header->size = g_ntohl(*size_p);
    if (header->size > 0x100000) {
        /* header too large */
        header->async_operation.Finished();
        istream_close_handler(header->input);

        GError *error =
            g_error_new_literal(sink_header_quark(), 0,
                                "header is too large");
        header->handler->error(error, header->handler_ctx);
        istream_deinit(&header->output);
        return 0;
    }

    if (header->size > 0) {
        header->buffer = (unsigned char *)
            p_malloc(header->output.pool, header->size);
        header->state = sink_header::HEADER;
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
    sink_header *header = (sink_header *)ctx;
    const unsigned char *data = (const unsigned char *)data0;
    size_t consumed = 0, nbytes;

    if (header->state == sink_header::DATA)
        return istream_invoke_data(&header->output, data, length);

    if (header->state == sink_header::SIZE) {
        header->pending = 0; /* just in case the callback is invoked */

        consumed = header_consume_size(header, data, length);
        if (consumed == 0)
            return 0;

        if (consumed == length)
            return length;

        data += consumed;
        length -= consumed;
    }

    if (header->state == sink_header::HEADER) {
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

    if (header->state == sink_header::DATA && length > 0) {
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
sink_header_input_direct(istream_direct type, int fd, size_t max_length,
                         void *ctx)
{
    sink_header *header = (sink_header *)ctx;

    assert(header->state == sink_header::DATA);

    return istream_invoke_direct(&header->output, type, fd, max_length);
}

static void
sink_header_input_eof(void *ctx)
{
    sink_header *header = (sink_header *)ctx;

    switch (header->state) {
        GError *error;

    case sink_header::SIZE:
    case sink_header::HEADER:
        header->async_operation.Finished();

        error = g_error_new_literal(sink_header_quark(), 0,
                                    "premature end of file");
        header->handler->error(error, header->handler_ctx);
        istream_deinit(&header->output);
        break;

    case sink_header::CALLBACK:
        assert(false);
        gcc_unreachable();

    case sink_header::DATA:
        istream_deinit_eof(&header->output);
        break;
    }
}

static void
sink_header_input_abort(GError *error, void *ctx)
{
    sink_header *header = (sink_header *)ctx;

    switch (header->state) {
    case sink_header::SIZE:
    case sink_header::HEADER:
        header->async_operation.Finished();
        header->handler->error(error, header->handler_ctx);
        istream_deinit(&header->output);
        break;

    case sink_header::CALLBACK:
        assert(false);
        gcc_unreachable();

    case sink_header::DATA:
        istream_deinit_abort(&header->output, error);
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
istream_to_header(struct istream *istream)
{
    void *p = ((char *)istream) - offsetof(struct sink_header, output);
    return (struct sink_header *)p;
}

static off_t
sink_header_available(struct istream *istream, bool partial)
{
    struct sink_header *header = istream_to_header(istream);
    off_t available = istream_available(header->input, partial);

    if (available >= 0 && header->state == sink_header::CALLBACK) {
        if (available < (off_t)header->pending) {
            assert(partial);

            return -1;
        }

        available -= header->pending;
    }

    return available;
}

static void
sink_header_read(struct istream *istream)
{
    struct sink_header *header = istream_to_header(istream);

    if (header->state == sink_header::CALLBACK)
        /* workaround: when invoking the callback from the data()
           handler, it would be illegal to call header->input again */
        return;

    istream_handler_set_direct(header->input, header->output.handler_direct);
    istream_read(header->input);
}

static void
sink_header_close(struct istream *istream)
{
    struct sink_header *header = istream_to_header(istream);

    istream_free_handler(&header->input);
    istream_deinit(&header->output);
}

static const struct istream_class istream_sink = {
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
    void *p = ((char *)ao) - offsetof(struct sink_header, async_operation);
    return (struct sink_header *)p;
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
sink_header_new(struct pool *pool, struct istream *input,
                const struct sink_header_handler *handler, void *ctx,
                struct async_operation_ref *async_ref)
{
    struct sink_header *header = (struct sink_header *)
        istream_new(pool, &istream_sink, sizeof(*header));

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(handler != NULL);
    assert(handler->done != NULL);
    assert(handler->error != NULL);

    istream_assign_handler(&header->input, input,
                           &sink_header_input_handler, header,
                           0);

    header->state = sink_header::SIZE;
    header->position = 0;
    header->handler = handler;
    header->handler_ctx = ctx;

    header->async_operation.Init(sink_header_operation);
    async_ref->Set(header->async_operation);
}
