#include "sink_buffer.hxx"
#include "istream_oo.hxx"
#include "istream_pointer.hxx"
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
    IstreamPointer input;

    unsigned char *const buffer;
    const size_t size;
    size_t position = 0;

    const struct sink_buffer_handler *const handler;
    void *handler_ctx;

    struct async_operation operation;

    BufferSink(struct pool &_pool, struct istream &_input, size_t available,
               const struct sink_buffer_handler &_handler, void *ctx,
               struct async_operation_ref &async_ref)
        :pool(&_pool),
         input(_input,
               MakeIstreamHandler<BufferSink>::handler, this,
               FD_ANY),
         buffer((unsigned char *)p_malloc(pool, available)),
         size(available),
         handler(&_handler), handler_ctx(ctx) {
        operation.Init2<BufferSink>();
        async_ref.Set(operation);
    }

    void Abort();

    /* istream handler */

    size_t OnData(const void *data, size_t length);
    ssize_t OnDirect(FdType type, int fd, size_t max_length);;
    void OnEof();
    void OnError(GError *error);
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

inline size_t
BufferSink::OnData(const void *data, size_t length)
{
    assert(position < size);
    assert(length <= size - position);

    memcpy(buffer + position, data, length);
    position += length;

    return length;
}

inline ssize_t
BufferSink::OnDirect(FdType type, int fd, size_t max_length)
{
    size_t length = size - position;
    if (length > max_length)
        length = max_length;

    ssize_t nbytes = IsAnySocket(type)
        ? recv(fd, buffer + position, length, MSG_DONTWAIT)
        : read(fd, buffer + position, length);
    if (nbytes > 0)
        position += (size_t)nbytes;

    return nbytes;
}

inline void
BufferSink::OnEof()
{
    assert(position == size);

    operation.Finished();
    handler->done(buffer, size, handler_ctx);
}

inline void
BufferSink::OnError(GError *error)
{
    operation.Finished();
    handler->error(error, handler_ctx);
}


/*
 * async operation
 *
 */

inline void
BufferSink::Abort()
{
    const ScopePoolRef ref(*pool TRACE_ARGS);
    input.Close();
}


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

    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(handler != nullptr);
    assert(handler->done != nullptr);
    assert(handler->error != nullptr);

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

    NewFromPool<BufferSink>(*pool, *pool, *input, available,
                            *handler, ctx, *async_ref);
}
