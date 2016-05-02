#include "RootPool.hxx"
#include "growing_buffer.hxx"
#include "direct.hxx"
#include "istream_gb.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "event/Base.hxx"
#include "util/ConstBuffer.hxx"

#include <glib.h>

#include <stdio.h>

struct Context final : IstreamHandler {
    struct pool *pool;
    bool got_data = false, eof = false, abort = false, closed = false;
    IstreamPointer abort_istream;

    explicit Context(struct pool &_pool)
        :pool(&_pool), abort_istream(nullptr) {}

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;
};

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length)
{
    got_data = true;

    if (abort_istream.IsDefined()) {
        closed = true;
        abort_istream.ClearAndClose();
        pool_unref(pool);
        return 0;
    }

    return length;
}

void
Context::OnEof()
{
    eof = true;

    pool_unref(pool);
}

void
Context::OnError(GError *error)
{
    g_error_free(error);

    abort = true;

    pool_unref(pool);
}

/*
 * utils
 *
 */

static int
istream_read_event(IstreamPointer &istream)
{
    istream.Read();
    return event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
}

static void
istream_read_expect(Context *ctx, IstreamPointer &istream)
{
    int ret;

    assert(!ctx->eof);

    ctx->got_data = false;

    ret = istream_read_event(istream);
    assert(ctx->eof || ctx->got_data || ret == 0);

    /* give istream_later another chance to breathe */
    event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
}

static void
run_istream_ctx(Context *ctx, struct pool *pool, Istream *_istream)
{
    gcc_unused off_t a1 = _istream->GetAvailable(false);
    gcc_unused off_t a2 = _istream->GetAvailable(true);

    IstreamPointer istream(*_istream, *ctx);

#ifndef NO_GOT_DATA_ASSERT
    while (!ctx->eof)
        istream_read_expect(ctx, istream);
#else
    for (int i = 0; i < 1000 && !ctx->eof; ++i)
           istream_read_event(istream);
#endif

    if (!ctx->eof) {
        pool_trash(pool);
        pool_unref(pool);
    }

    pool_commit();
}

static void
run_istream(struct pool *pool, Istream *istream)
{
    Context ctx(*pool);
    run_istream_ctx(&ctx, pool, istream);
}

static Istream *
create_test(struct pool *pool)
{
    GrowingBuffer *gb = growing_buffer_new(pool, 64);
    growing_buffer_write_string(gb, "foo");
    return istream_gb_new(*pool, *gb);
}

static Istream *
create_empty(struct pool *pool)
{
    GrowingBuffer *gb = growing_buffer_new(pool, 64);
    return istream_gb_new(*pool, *gb);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(struct pool *pool)
{
    Istream *istream;

    pool = pool_new_linear(pool, "test", 8192);
    istream = create_test(pool);

    run_istream(pool, istream);
}

/** empty input */
static void
test_empty(struct pool *pool)
{
    Istream *istream;

    pool = pool_new_linear(pool, "test", 8192);
    istream = create_empty(pool);

    run_istream(pool, istream);
}

/** first buffer is too small, empty */
static void
test_first_empty(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    GrowingBuffer *buffer = growing_buffer_new(pool, 16);
    GrowingBufferReader reader(*buffer);

    growing_buffer_write_string(buffer, "0123456789abcdefg");

    auto x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 17);

    reader.Consume(x.size);

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** test growing_buffer_reader_skip() */
static void
test_skip(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    GrowingBuffer *buffer = growing_buffer_new(pool, 3);
    GrowingBufferReader reader(*buffer);

    growing_buffer_write_string(buffer, "0123");
    growing_buffer_write_string(buffer, "4567");
    growing_buffer_write_string(buffer, "89ab");
    growing_buffer_write_string(buffer, "cdef");

    reader.Skip(6);

    auto x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 2);
    reader.Consume(1);

    reader.Skip(5);

    x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 4);
    reader.Consume(4);

    x = reader.Read();
    assert(x.IsNull());

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** test reading the head while appending to the tail */
static void
test_concurrent_rw(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    GrowingBuffer *buffer = growing_buffer_new(pool, 3);
    GrowingBufferReader reader(*buffer);

    growing_buffer_write_string(buffer, "0123");
    growing_buffer_write_string(buffer, "4567");
    growing_buffer_write_string(buffer, "89ab");
    assert(reader.Available() == 12);

    reader.Skip(12);
    assert(reader.IsEOF());
    assert(reader.Available() == 0);

    growing_buffer_write_string(buffer, "cdef");
    reader.Update();

    assert(!reader.IsEOF());
    assert(reader.Available() == 4);

    auto x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 4);

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** abort without handler */
static void
test_abort_without_handler(struct pool *pool)
{
    Istream *istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool);
    istream->CloseUnused();

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** abort with handler */
static void
test_abort_with_handler(struct pool *pool)
{
    Context ctx(*pool);

    ctx.pool = pool_new_linear(pool, "test", 8192);

    Istream *istream = create_test(ctx.pool);
    istream->SetHandler(ctx);

    istream->Close();
    pool_unref(ctx.pool);

    assert(!ctx.abort);

    pool_commit();
}

/** abort in handler */
static void
test_abort_in_handler(struct pool *pool)
{
    Context ctx(*pool_new_linear(pool, "test", 8192));

    ctx.abort_istream.Set(*create_test(ctx.pool), ctx);

    while (!ctx.eof && !ctx.abort && !ctx.closed) {
        istream_read_expect(&ctx, ctx.abort_istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(!ctx.abort_istream.IsDefined());
    assert(!ctx.abort);
    assert(ctx.closed);

    pool_commit();
}

/*
 * main
 *
 */


int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    direct_global_init();
    EventBase event_base;

    /* run test suite */

    test_normal(RootPool());
    test_empty(RootPool());
    test_first_empty(RootPool());
    test_skip(RootPool());
    test_concurrent_rw(RootPool());
    test_abort_without_handler(RootPool());
    test_abort_with_handler(RootPool());
    test_abort_in_handler(RootPool());
}
