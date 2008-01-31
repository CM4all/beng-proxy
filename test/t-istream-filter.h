#include <event.h>

#include <stdio.h>

#ifndef FILTER_CLEANUP
static void
cleanup(void)
{
}
#endif

struct ctx {
    istream_t input;
};

static int got_data, should_exit;

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *_ctx)
{
    struct ctx *ctx = _ctx;

    (void)data;
    (void)ctx;
    printf("data(%zu)\n", length);
    got_data = 1;

    if (ctx != NULL) {
        istream_free(&ctx->input);
        return 0;
    }

    return length;
}

static ssize_t
my_istream_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    (void)fd;
    (void)ctx;
    printf("direct(%u, %zu)\n", type, max_length);
    got_data = 1;
    return max_length;
}

static void
my_istream_eof(void *ctx)
{
    (void)ctx;
    printf("eof\n");
    should_exit = 1;
}

static void
my_istream_abort(void *ctx)
{
    (void)ctx;
    printf("abort\n");
    should_exit = 1;
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

static void
istream_read_expect(istream_t istream)
{
    assert(!should_exit);
    got_data = 0;
    istream_read(istream);
    assert(should_exit || got_data);
}

static void
run_istream(istream_t istream)
{
    pool_t pool = istream_pool(istream);

    should_exit = 0;

    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    while (!should_exit) {
        istream_read_expect(istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    pool_unref(pool);
    pool_commit();

    cleanup();
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
    run_istream(istream);
}

/** test with istream_byte */
static void
test_byte(pool_t pool)
{
    istream_t istream;

    should_exit = 0;
    got_data = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_byte_new(pool, create_input(pool)));
    run_istream(istream);
}

/** input fails */
static void
test_fail(pool_t pool)
{
    istream_t istream;

    should_exit = 0;
    got_data = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_fail_new(pool));
    run_istream(istream);
}

/** input fails after the first byte */
static void
test_fail_1byte(pool_t pool)
{
    istream_t istream;

    should_exit = 0;
    got_data = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool,
                          istream_cat_new(pool,
                                          istream_head_new(pool, create_input(pool), 1),
                                          istream_fail_new(pool),
                                          NULL));
    run_istream(istream);
}

/** abort without handler */
static void
test_abort_without_handler(pool_t pool)
{
    istream_t istream;

    should_exit = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_close(istream);

    pool_unref(pool);
    pool_commit();

    assert(!should_exit);

    cleanup();
}

/** abort with handler */
static void
test_abort_with_handler(pool_t pool)
{
    istream_t istream;

    should_exit = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, create_input(pool));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    istream_close(istream);

    pool_unref(pool);
    pool_commit();

    assert(should_exit);

    cleanup();
}

/** abort in handler */
static void
test_abort_in_handler(pool_t pool)
{
    istream_t istream;
    struct ctx ctx;

    should_exit = 0;

    pool = pool_new_linear(pool, "test", 8192);

    ctx.input = create_test(pool, create_input(pool));
    istream_handler_set(ctx.input, &my_istream_handler, &ctx, 0);

    while (!should_exit) {
        istream_read_expect(istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.input == NULL);

    pool_unref(pool);
    pool_commit();

    cleanup();
}

/** abort after 1 byte of output */
static void
test_abort_1byte(pool_t pool)
{
    istream_t istream;

    should_exit = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = istream_head_new(pool,
                               create_test(pool,
                                           create_input(pool)),
                               1);
    run_istream(istream);
}

/** test with istream_later filter */
static void
test_later(pool_t pool)
{
    istream_t istream;

    should_exit = 0;
    got_data = 0;

    pool = pool_new_linear(pool, "test", 8192);

    istream = create_test(pool, istream_later_new(pool, create_input(pool)));
    istream_handler_set(istream, &my_istream_handler, NULL, 0);

    while (!should_exit) {
        istream_read(istream);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    pool_unref(pool);
    pool_commit();

    cleanup();
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

    event_base = event_init();

    root_pool = pool_new_libc(NULL, "root");

    /* run test suite */

    test_normal(root_pool);
    test_byte(root_pool);
    test_fail(root_pool);
    test_fail_1byte(root_pool);
    test_abort_without_handler(root_pool);
    test_abort_with_handler(root_pool);
    test_abort_in_handler(root_pool);
    test_abort_1byte(root_pool);
    test_later(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
