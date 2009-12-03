#include "direct.h"

#include <event.h>

#include <stdio.h>
#ifdef EXPECTED_RESULT
#include <string.h>
#endif

enum {
#ifdef NO_BLOCKING
    enable_blocking = false,
#else
    enable_blocking = true,
#endif
};

#ifndef FILTER_CLEANUP
static void
cleanup(void)
{
}
#endif

struct ctx {
    bool half;
    bool got_data, eof;
#ifdef EXPECTED_RESULT
    bool record;
    char buffer[sizeof(EXPECTED_RESULT) * 2];
    size_t buffer_length;
#endif
    istream_t abort_istream;
    int abort_after;

    int block_after;
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

    printf("data(%zu)\n", length);
    ctx->got_data = true;

    if (ctx->abort_istream != NULL && ctx->abort_after-- == 0) {
        istream_free(&ctx->abort_istream);
        return 0;
    }

    if (ctx->half && length > 8)
        length = (length + 1) / 2;

    if (ctx->block_after >= 0) {
        --ctx->block_after;
        if (ctx->block_after == -1)
            /* block once */
            return 0;
    }

#ifdef EXPECTED_RESULT
    if (ctx->record) {
        assert(ctx->buffer_length + length < sizeof(ctx->buffer));
        assert(memcmp(EXPECTED_RESULT + ctx->buffer_length, data, length) == 0);

        if (ctx->buffer_length + length < sizeof(ctx->buffer))
            memcpy(ctx->buffer + ctx->buffer_length, data, length);
        ctx->buffer_length += length;
    }
#endif

    return length;
}

static ssize_t
my_istream_direct(istream_direct_t type, int fd, size_t max_length, void *_ctx)
{
    struct ctx *ctx = _ctx;

    (void)fd;

    printf("direct(%u, %zu)\n", type, max_length);
    ctx->got_data = true;

    if (ctx->abort_istream != NULL) {
        istream_free(&ctx->abort_istream);
        return 0;
    }

    return max_length;
}

static void
my_istream_eof(void *_ctx)
{
    struct ctx *ctx = _ctx;

    printf("eof\n");
    ctx->eof = true;
}

static void
my_istream_abort(void *_ctx)
{
    struct ctx *ctx = _ctx;

#ifdef EXPECTED_RESULT
    assert(!ctx->record);
#endif

    printf("abort\n");
    ctx->eof = true;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .direct = my_istream_direct,
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

    pool_unref(pool);
    pool_commit();

#ifndef NO_GOT_DATA_ASSERT
    while (!ctx->eof)
        istream_read_expect(ctx, istream);
#else
    for (int i = 0; i < 1000 && !ctx->eof; ++i)
           istream_read_event(istream);
#endif

#ifdef EXPECTED_RESULT
    if (ctx->record) {
        assert(ctx->buffer_length == sizeof(EXPECTED_RESULT) - 1);
        assert(memcmp(ctx->buffer, EXPECTED_RESULT, ctx->buffer_length) == 0);
    }
#endif

    cleanup();
    pool_commit();
}

static void
run_istream_block(pool_t pool, istream_t istream, bool record __attr_unused,
                  int block_after)
{
    struct ctx ctx = {
        .abort_istream = NULL,
        .block_after = block_after,
#ifdef EXPECTED_RESULT
        .record = record,
#endif
    };

    run_istream_ctx(&ctx, pool, istream);
}

static void
run_istream(pool_t pool, istream_t istream, bool record)
{
    run_istream_block(pool, istream, record, -1);
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

    istream = create_test(pool, create_input(pool));
    assert(istream != NULL);
    assert(!istream_has_handler(istream));

    run_istream(pool, istream, true);
}

/** block once after n data() invocations */
static void
test_block(pool_t parent_pool)
{
    pool_t pool;

    for (int n = 0; n < 8; ++n) {
        istream_t istream;

        pool = pool_new_linear(parent_pool, "test", 8192);

        istream = create_test(pool, create_input(pool));
        assert(istream != NULL);
        assert(!istream_has_handler(istream));

        run_istream_block(pool, istream, true, n);
    }
}

/** test with istream_byte */
static void
test_byte(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_byte_new(pool, create_input(pool)));
    run_istream(pool, istream, true);
}

/** accept only half of the data */
static void
test_half(pool_t pool)
{
    struct ctx ctx = {
        .eof = false,
        .half = true,
#ifdef EXPECTED_RESULT
        .record = true,
#endif
        .abort_istream = NULL,
        .block_after = -1,
    };

    pool = pool_new_linear(pool, "test", 8192);

    run_istream_ctx(&ctx, pool, create_test(pool, create_input(pool)));
}

/** input fails */
static void
test_fail(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_fail_new(pool));
    run_istream(pool, istream, false);
}

/** input fails after the first byte */
static void
test_fail_1byte(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool,
                          istream_cat_new(pool,
                                          istream_head_new(pool, create_input(pool), 1),
                                          istream_fail_new(pool),
                                          NULL));
    run_istream(pool, istream, false);
}

/** abort without handler */
static void
test_abort_without_handler(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    pool_unref(pool);
    pool_commit();

    istream_close(istream);

    cleanup();
    pool_commit();
}

/** abort with handler */
static void
test_abort_with_handler(pool_t pool)
{
    struct ctx ctx = {
        .abort_istream = NULL,
        .block_after = -1,
        .eof = false,
        .half = false,
#ifdef EXPECTED_RESULT
        .record = false,
#endif
    };
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_handler_set(istream, &my_istream_handler, &ctx, 0);
    pool_unref(pool);
    pool_commit();

    istream_close(istream);

    cleanup();
    pool_commit();

    assert(ctx.eof);
}

/** abort in handler */
static void
test_abort_in_handler(pool_t pool)
{
    struct ctx ctx = {
        .eof = false,
        .half = false,
#ifdef EXPECTED_RESULT
        .record = false,
#endif
        .abort_after = 0,
        .block_after = -1,
    };

    pool = pool_new_linear(pool, "test", 8192);

    ctx.abort_istream = create_test(pool, create_input(pool));
    istream_handler_set(ctx.abort_istream, &my_istream_handler, &ctx, 0);
    pool_unref(pool);
    pool_commit();

    while (!ctx.eof) {
        istream_read_expect(&ctx, ctx.abort_istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == NULL);

    cleanup();
    pool_commit();
}

/** abort in handler, with some data consumed */
static void
test_abort_in_handler_half(pool_t pool)
{
    struct ctx ctx = {
        .eof = false,
        .half = true,
#ifdef EXPECTED_RESULT
        .record = false,
#endif
        .abort_after = 2,
        .block_after = -1,
    };

    pool = pool_new_linear(pool, "test", 8192);

    ctx.abort_istream = create_test(pool, istream_four_new(pool, create_input(pool)));
    istream_handler_set(ctx.abort_istream, &my_istream_handler, &ctx, 0);
    pool_unref(pool);
    pool_commit();

    while (!ctx.eof) {
        istream_read_expect(&ctx, ctx.abort_istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == NULL || ctx.abort_after >= 0);

    cleanup();
    pool_commit();
}

/** abort after 1 byte of output */
static void
test_abort_1byte(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = istream_head_new(pool,
                               create_test(pool,
                                           create_input(pool)),
                               1);
    run_istream(pool, istream, false);
}

/** test with istream_later filter */
static void
test_later(pool_t pool)
{
    istream_t istream;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_later_new(pool, create_input(pool)));
    run_istream(pool, istream, true);
}

#ifdef EXPECTED_RESULT
/** test with large input and blocking handler */
static void
test_big_hold(pool_t pool)
{
    istream_t istream, hold;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_input(pool);
    for (unsigned i = 0; i < 1024; ++i)
        istream = istream_cat_new(pool, istream, create_input(pool), NULL);

    istream = create_test(pool, istream);
    hold = istream_hold_new(pool, istream);

    istream_read(istream);

    istream_close(hold);

    pool_unref(pool);
}
#endif


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
    if (enable_blocking)
        test_block(root_pool);
    if (enable_blocking)
        test_byte(root_pool);
    test_half(root_pool);
    test_fail(root_pool);
    test_fail_1byte(root_pool);
    test_abort_without_handler(root_pool);
    test_abort_with_handler(root_pool);
    test_abort_in_handler(root_pool);
    if (enable_blocking)
        test_abort_in_handler_half(root_pool);
    test_abort_1byte(root_pool);
    test_later(root_pool);

#ifdef EXPECTED_RESULT
    test_big_hold(root_pool);
#endif

#ifdef CUSTOM_TEST
    test_custom(root_pool);
#endif

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
