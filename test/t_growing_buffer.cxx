#include "RootPool.hxx"
#include "GrowingBuffer.hxx"
#include "direct.hxx"
#include "istream_gb.hxx"
#include "istream/istream.hxx"
#include "istream/Pointer.hxx"
#include "fb_pool.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <glib.h>

#include <string.h>
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

static void
istream_read_expect(Context *ctx, IstreamPointer &istream)
{
    assert(!ctx->eof);

    ctx->got_data = false;

    istream.Read();
    assert(ctx->eof || ctx->got_data);
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
        istream->Read();
#endif

    if (!ctx->eof && !ctx->abort)
        istream.Close();

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
    GrowingBuffer gb;
    gb.Write("foo");
    return istream_gb_new(*pool, std::move(gb));
}

static Istream *
create_empty(struct pool *pool)
{
    GrowingBuffer gb;
    return istream_gb_new(*pool, std::move(gb));
}

static bool
Equals(WritableBuffer<void> a, const char *b)
{
    return a.size == strlen(b) && memcmp(a.data, b, a.size) == 0;
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
    GrowingBuffer buffer;

    buffer.Write("0123456789abcdefg");

    assert(buffer.GetSize() == 17);
    assert(Equals(buffer.Dup(*pool), "0123456789abcdefg"));

    GrowingBufferReader reader(std::move(buffer));
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
    GrowingBuffer buffer;

    buffer.Write("0123");
    buffer.Write("4567");
    buffer.Write("89ab");
    buffer.Write("cdef");

    assert(buffer.GetSize() == 16);
    assert(Equals(buffer.Dup(*pool), "0123456789abcdef"));

    constexpr size_t buffer_size = 8192 - 2 * sizeof(void*) - 2 * sizeof(size_t);

    static char zero[buffer_size * 2];
    buffer.Write(zero, sizeof(zero));
    assert(buffer.GetSize() == 16 + buffer_size * 2);

    GrowingBufferReader reader(std::move(buffer));
    reader.Skip(buffer_size - 2);

    auto x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 2);
    reader.Consume(1);

    reader.Skip(5);

    x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == buffer_size - 4);
    reader.Consume(4);

    x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == buffer_size - 8);

    reader.Skip(buffer_size);

    x = reader.Read();
    assert(!x.IsNull());
    assert(x.size == 8);

    reader.Skip(8);

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
    GrowingBuffer buffer;

    buffer.Write("0123");
    buffer.Write("4567");
    buffer.Write("89ab");

    assert(buffer.GetSize() == 12);
    assert(Equals(buffer.Dup(*pool), "0123456789ab"));

    buffer.Skip(12);
    assert(buffer.IsEmpty());
    assert(buffer.GetSize() == 0);

    buffer.Write("cdef");

    assert(!buffer.IsEmpty());
    assert(buffer.GetSize() == 4);
    assert(Equals(buffer.Dup(*pool), "cdef"));

    auto x = buffer.Read();
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

    while (!ctx.eof && !ctx.abort && !ctx.closed)
        istream_read_expect(&ctx, ctx.abort_istream);

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
    const ScopeFbPoolInit fb_pool_init;

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
