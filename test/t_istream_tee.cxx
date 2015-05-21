#include "istream_tee.hxx"
#include "istream_delayed.hxx"
#include "istream_string.hxx"
#include "istream.h"
#include "async.hxx"
#include "sink_gstring.hxx"
#include "sink-impl.h"

#include <glib.h>

#include <event.h>
#include <string.h>

struct ctx {
    GString *value;

    bool eof, aborted;
};

/*
 * istream handler
 *
 */

static size_t
my_istream_data(const void *data, size_t length, void *ctx)
{
    (void)data;
    (void)length;
    (void)ctx;

    return 0;
}

static void
my_istream_eof(void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    ctx->eof = true;
}

static void
my_istream_abort(GError *error, void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    g_error_free(error);

    ctx->aborted = true;
}

static const struct istream_handler block_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * tests
 *
 */

static void
buffer_callback(GString *value, GError *error, void *_ctx)
{
    struct ctx *ctx = (struct ctx *)_ctx;

    assert(value != nullptr);
    assert(error == nullptr);

    ctx->value = value;
}

static void
test_block1(struct pool *pool)
{
    struct ctx ctx = {
        .value = nullptr,
        .eof = false,
        .aborted = false,
    };
    struct async_operation_ref async_ref;

    struct istream *delayed = istream_delayed_new(pool);
    struct istream *tee = istream_tee_new(pool, delayed, false, false);
    struct istream *second = istream_tee_second(tee);

    istream_handler_set(tee, &block_istream_handler, &ctx, 0);

    sink_gstring_new(pool, second, buffer_callback, &ctx, &async_ref);
    assert(ctx.value == nullptr);

    /* the input (istream_delayed) blocks */
    istream_read(second);
    assert(ctx.value == nullptr);

    /* feed data into input */
    istream_delayed_set(delayed, istream_string_new(pool, "foo"));
    assert(ctx.value == nullptr);

    /* the first output (block_istream_handler) blocks */
    istream_read(second);
    assert(ctx.value == nullptr);

    /* close the blocking output, this should release the "tee"
       object and restart reading (into the second output) */
    assert(!ctx.aborted && !ctx.eof);
    istream_free_handler(&tee);
    assert(!ctx.aborted && !ctx.eof);
    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);
}

static void
test_close_data(struct pool *pool)
{
    struct ctx ctx = {
        .value = nullptr,
        .eof = false,
        .aborted = false,
    };
    struct async_operation_ref async_ref;

    struct istream *tee =
        istream_tee_new(pool, istream_string_new(pool, "foo"), false, false);

    sink_close_new(tee);
    struct istream *second = istream_tee_second(tee);

    sink_gstring_new(pool, second, buffer_callback, &ctx, &async_ref);
    assert(ctx.value == nullptr);

    istream_read(second);

    /* at this point, sink_close has closed itself, and istream_tee
       should have passed the data to the sink_gstring */

    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);
}

/**
 * Close the second output after data has been consumed only by the
 * first output.  This verifies that istream_tee's "skip" attribute is
 * obeyed properly.
 */
static void
test_close_skipped(struct pool *pool)
{
    struct ctx ctx = {
        .value = nullptr,
        .eof = false,
        .aborted = false,
    };
    struct async_operation_ref async_ref;

    struct istream *input = istream_string_new(pool, "foo");
    struct istream *tee = istream_tee_new(pool, input, false, false);
    sink_gstring_new(pool, tee, buffer_callback, &ctx, &async_ref);

    struct istream *second = istream_tee_second(tee);
    sink_close_new(second);

    assert(ctx.value == nullptr);

    istream_read(input);

    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);
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

    event_base = event_init();

    root_pool = pool_new_libc(nullptr, "root");

    /* run test suite */

    test_block1(root_pool);
    test_close_data(root_pool);
    test_close_skipped(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
