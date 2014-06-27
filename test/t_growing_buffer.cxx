#include "growing_buffer.hxx"
#include "direct.h"
#include "istream_gb.hxx"
#include "istream.h"
#include "util/ConstBuffer.hxx"

#include <event.h>

#include <glib.h>

#include <stdio.h>

struct ctx {
    struct pool *pool;
    bool got_data, eof, abort, closed;
    struct istream *abort_istream;
};

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    (void)data;

    ctx->got_data = true;

    if (ctx->abort_istream != nullptr) {
        ctx->closed = true;
        istream_free_handler(&ctx->abort_istream);
        pool_unref(ctx->pool);
        return 0;
    }

    return length;
}

static void
my_istream_eof(void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    ctx->eof = true;

    pool_unref(ctx->pool);
}

static void
my_istream_abort(GError *error, void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    g_error_free(error);

    ctx->abort = true;

    pool_unref(ctx->pool);
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * utils
 *
 */

static int
istream_read_event(struct istream *istream)
{
    istream_read(istream);
    return event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
}

static void
istream_read_expect(struct ctx *ctx, struct istream *istream)
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
run_istream_ctx(struct ctx *ctx, struct pool *pool, struct istream *istream)
{
    ctx->eof = false;

    gcc_unused off_t a1 = istream_available(istream, false);
    gcc_unused off_t a2 = istream_available(istream, true);

    istream_handler_set(istream, &my_istream_handler, ctx, 0);

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
run_istream(struct pool *pool, struct istream *istream)
{
    struct ctx ctx = {
        .pool = pool,
        .abort_istream = nullptr,
    };

    run_istream_ctx(&ctx, pool, istream);
}

static struct istream *
create_test(struct pool *pool)
{
    struct growing_buffer *gb = growing_buffer_new(pool, 64);
    growing_buffer_write_string(gb, "foo");
    return istream_gb_new(pool, gb);
}

static struct istream *
create_empty(struct pool *pool)
{
    struct growing_buffer *gb = growing_buffer_new(pool, 64);
    return istream_gb_new(pool, gb);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(struct pool *pool)
{
    struct istream *istream;

    pool = pool_new_linear(pool, "test", 8192);
    istream = create_test(pool);

    run_istream(pool, istream);
}

/** empty input */
static void
test_empty(struct pool *pool)
{
    struct istream *istream;

    pool = pool_new_linear(pool, "test", 8192);
    istream = create_empty(pool);

    run_istream(pool, istream);
}

/** first buffer is too small, empty */
static void
test_first_empty(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    struct growing_buffer *buffer = growing_buffer_new(pool, 16);
    struct growing_buffer_reader reader(*buffer);

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
    struct growing_buffer *buffer = growing_buffer_new(pool, 3);
    struct growing_buffer_reader reader(*buffer);

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
    struct growing_buffer *buffer = growing_buffer_new(pool, 3);
    struct growing_buffer_reader reader(*buffer);

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
    struct istream *istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool);
    istream_close_unused(istream);

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** abort with handler */
static void
test_abort_with_handler(struct pool *pool)
{
    struct ctx ctx = {
        .abort_istream = nullptr,
        .eof = false,
    };
    struct istream *istream;

    ctx.pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(ctx.pool);
    istream_handler_set(istream, &my_istream_handler, &ctx, 0);

    istream_free_handler(&istream);
    pool_unref(ctx.pool);

    assert(!ctx.abort);

    pool_commit();
}

/** abort in handler */
static void
test_abort_in_handler(struct pool *pool)
{
    struct ctx ctx = {
        .eof = false,
    };

    ctx.pool = pool_new_linear(pool, "test", 8192);

    ctx.abort_istream = create_test(ctx.pool);
    istream_handler_set(ctx.abort_istream, &my_istream_handler, &ctx, 0);

    while (!ctx.eof && !ctx.abort && !ctx.closed) {
        istream_read_expect(&ctx, ctx.abort_istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == nullptr);
    assert(!ctx.abort);
    assert(ctx.closed);

    pool_commit();
}

/*
 * main
 *
 */


int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *root_pool;

    (void)argc;
    (void)argv;

    direct_global_init();
    event_base = event_init();

    root_pool = pool_new_libc(nullptr, "root");

    /* run test suite */

    test_normal(root_pool);
    test_empty(root_pool);
    test_first_empty(root_pool);
    test_skip(root_pool);
    test_concurrent_rw(root_pool);
    test_abort_without_handler(root_pool);
    test_abort_with_handler(root_pool);
    test_abort_in_handler(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
