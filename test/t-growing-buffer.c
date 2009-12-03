#include "growing-buffer.h"
#include "direct.h"

#include <event.h>

#include <stdio.h>

struct ctx {
    pool_t pool;
    bool got_data, eof, abort;
    istream_t abort_istream;
};

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *_ctx)
{
    struct ctx *ctx = _ctx;

    (void)data;

    ctx->got_data = true;

    if (ctx->abort_istream != NULL) {
        istream_free(&ctx->abort_istream);
        return 0;
    }

    return length;
}

static void
my_istream_eof(void *_ctx)
{
    struct ctx *ctx = _ctx;

    ctx->eof = true;

    pool_unref(ctx->pool);
}

static void
my_istream_abort(void *_ctx)
{
    struct ctx *ctx = _ctx;

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
istream_read_event(istream_t istream)
{
    istream_read(istream);
    return event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
}

static void
istream_read_expect(struct ctx *ctx, istream_t istream)
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
run_istream_ctx(struct ctx *ctx, pool_t pool, istream_t istream)
{
    ctx->eof = false;

    istream_available(istream, false);
    istream_available(istream, true);

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
run_istream(pool_t pool, istream_t istream)
{
    struct ctx ctx = {
        .pool = pool,
        .abort_istream = NULL,
    };

    run_istream_ctx(&ctx, pool, istream);
}

static istream_t
create_test(pool_t pool)
{
    struct growing_buffer *gb = growing_buffer_new(pool, 64);
    growing_buffer_write_string(gb, "foo");
    return growing_buffer_istream(gb);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);
    istream = create_test(pool);

    run_istream(pool, istream);
}

/** abort without handler */
static void
test_abort_without_handler(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool);
    istream_close(istream);

    pool_trash(pool);
    pool_unref(pool);
    pool_commit();
}

/** abort with handler */
static void
test_abort_with_handler(pool_t pool)
{
    struct ctx ctx = {
        .abort_istream = NULL,
        .eof = false,
    };
    istream_t istream;

    ctx.pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(ctx.pool);
    istream_handler_set(istream, &my_istream_handler, &ctx, 0);

    istream_close(istream);

    assert(ctx.abort);

    pool_commit();
}

/** abort in handler */
static void
test_abort_in_handler(pool_t pool)
{
    struct ctx ctx = {
        .eof = false,
    };

    ctx.pool = pool_new_linear(pool, "test", 8192);

    ctx.abort_istream = create_test(ctx.pool);
    istream_handler_set(ctx.abort_istream, &my_istream_handler, &ctx, 0);

    while (!ctx.eof && !ctx.abort) {
        istream_read_expect(&ctx, ctx.abort_istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == NULL);
    assert(ctx.abort);

    pool_commit();
}

/*
 * main
 *
 */


int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t root_pool;

    (void)argc;
    (void)argv;

    direct_global_init();
    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");

    /* run test suite */

    test_normal(root_pool);
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
