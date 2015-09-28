/*
 * This istream filter reads a 32 bit header size from the stream,
 * reads it into a buffer and invokes a callback with the tail of the
 * stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_header.hxx"
#include "istream_pointer.hxx"
#include "pool.hxx"
#include "istream_internal.hxx"
#include "async.hxx"
#include "util/Cast.hxx"
#include "util/ByteOrder.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>
#include <stdint.h>

struct HeaderSink {
    struct istream output;

    enum {
        SIZE, HEADER, CALLBACK, DATA
    } state;

    IstreamPointer input;

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

    size_t InvokeCallback(size_t consumed);

    size_t ConsumeSize(const void *data, size_t length);
    size_t ConsumeHeader(const void *data, size_t length);
};

static GQuark
sink_header_quark(void)
{
    return g_quark_from_static_string("sink_header");
}

size_t
HeaderSink::InvokeCallback(size_t consumed)
{
    assert(state == SIZE || state == HEADER);

    async_operation.Finished();

    const ScopePoolRef ref(*output.pool TRACE_ARGS);

    /* the base value has been set by sink_header_input_data() */
    pending += consumed;

    state = CALLBACK;
    handler->done(buffer, size,
                  &output,
                  handler_ctx);

    if (input.IsDefined()) {
        state = DATA;
        input.SetDirect(output.handler_direct);
    } else
        /* we have been closed meanwhile; bail out */
        consumed = 0;

    return consumed;
}

inline size_t
HeaderSink::ConsumeSize(const void *data, size_t length)
{
    assert(position < sizeof(size_buffer));

    if (length > sizeof(size_buffer) - position)
        length = sizeof(size_buffer) - position;

    memcpy(size_buffer + position, data, length);
    position += length;

    if (position < sizeof(size_buffer))
        return length;

    const uint32_t *size_p = (const uint32_t *)(const void *)size_buffer;
    size = FromBE32(*size_p);
    if (size > 0x100000) {
        /* header too large */
        async_operation.Finished();
        input.Close();

        GError *error =
            g_error_new_literal(sink_header_quark(), 0,
                                "header is too large");
        handler->error(error, handler_ctx);
        istream_deinit(&output);
        return 0;
    }

    if (size > 0) {
        buffer = (unsigned char *)
            p_malloc(output.pool, size);
        state = HeaderSink::HEADER;
        position = 0;
    } else {
        /* header empty: don't allocate, invoke callback now */

        buffer = nullptr;

        length = InvokeCallback(length);
    }

    return length;
}

inline size_t
HeaderSink::ConsumeHeader(const void *data, size_t length)
{
    size_t nbytes = size - position;

    assert(position < size);

    if (nbytes > length)
        nbytes = length;

    memcpy(buffer + position, data, nbytes);
    position += nbytes;

    if (position < size)
        return nbytes;

    return InvokeCallback(nbytes);
}


/*
 * istream handler
 *
 */

static size_t
sink_header_input_data(const void *data0, size_t length, void *ctx)
{
    auto *header = (HeaderSink *)ctx;
    const unsigned char *data = (const unsigned char *)data0;
    size_t consumed = 0, nbytes;

    if (header->state == HeaderSink::DATA)
        return istream_invoke_data(&header->output, data, length);

    if (header->state == HeaderSink::SIZE) {
        header->pending = 0; /* just in case the callback is invoked */

        consumed = header->ConsumeSize(data, length);
        if (consumed == 0)
            return 0;

        if (consumed == length)
            return length;

        data += consumed;
        length -= consumed;
    }

    if (header->state == HeaderSink::HEADER) {
        header->pending = consumed; /* just in case the callback is invoked */

        nbytes = header->ConsumeHeader(data, length);
        if (nbytes == 0)
            return 0;

        consumed += nbytes;
        if (consumed == length)
            return length;

        data += nbytes;
        length -= nbytes;
    }

    assert(consumed > 0);

    if (header->state == HeaderSink::DATA && length > 0) {
        const ScopePoolRef ref(*header->output.pool TRACE_ARGS);

        nbytes = istream_invoke_data(&header->output, data, length);
        if (nbytes == 0 && !header->input.IsDefined())
            consumed = 0;
        else
            consumed += nbytes;
    }

    return consumed;
}

static ssize_t
sink_header_input_direct(FdType type, int fd, size_t max_length, void *ctx)
{
    auto *header = (HeaderSink *)ctx;

    assert(header->state == HeaderSink::DATA);

    return istream_invoke_direct(&header->output, type, fd, max_length);
}

static void
sink_header_input_eof(void *ctx)
{
    auto *header = (HeaderSink *)ctx;

    switch (header->state) {
        GError *error;

    case HeaderSink::SIZE:
    case HeaderSink::HEADER:
        header->async_operation.Finished();

        error = g_error_new_literal(sink_header_quark(), 0,
                                    "premature end of file");
        header->handler->error(error, header->handler_ctx);
        istream_deinit(&header->output);
        break;

    case HeaderSink::CALLBACK:
        assert(false);
        gcc_unreachable();

    case HeaderSink::DATA:
        istream_deinit_eof(&header->output);
        break;
    }
}

static void
sink_header_input_abort(GError *error, void *ctx)
{
    auto *header = (HeaderSink *)ctx;

    switch (header->state) {
    case HeaderSink::SIZE:
    case HeaderSink::HEADER:
        header->async_operation.Finished();
        header->handler->error(error, header->handler_ctx);
        istream_deinit(&header->output);
        break;

    case HeaderSink::CALLBACK:
        assert(false);
        gcc_unreachable();

    case HeaderSink::DATA:
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

static inline HeaderSink *
istream_to_header(struct istream *istream)
{
    return &ContainerCast2(*istream, &HeaderSink::output);
}

static off_t
sink_header_available(struct istream *istream, bool partial)
{
    HeaderSink *header = istream_to_header(istream);
    off_t available = header->input.GetAvailable(partial);

    if (available >= 0 && header->state == HeaderSink::CALLBACK) {
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
    HeaderSink *header = istream_to_header(istream);

    if (header->state == HeaderSink::CALLBACK)
        /* workaround: when invoking the callback from the data()
           handler, it would be illegal to call header->input again */
        return;

    header->input.SetDirect(header->output.handler_direct);
    header->input.Read();
}

static void
sink_header_close(struct istream *istream)
{
    HeaderSink *header = istream_to_header(istream);

    header->input.ClearAndClose();
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

static HeaderSink *
async_to_sink_header(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &HeaderSink::async_operation);
}

static void
sink_header_abort(struct async_operation *ao)
{
    HeaderSink *header = async_to_sink_header(ao);

    header->input.Close();
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
    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(handler != NULL);
    assert(handler->done != NULL);
    assert(handler->error != NULL);

    auto header = NewFromPool<HeaderSink>(*pool);
    istream_init(&header->output, &istream_sink, pool);

    header->input.Set(*input,
                      sink_header_input_handler, header);

    header->state = HeaderSink::SIZE;
    header->position = 0;
    header->handler = handler;
    header->handler_ctx = ctx;

    header->async_operation.Init(sink_header_operation);
    async_ref->Set(header->async_operation);
}
