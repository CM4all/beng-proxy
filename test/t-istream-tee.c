#include "istream.h"
#include "async.h"
#include "sink-gstring.h"

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
    struct ctx *ctx = _ctx;

    ctx->eof = true;
}

static void
my_istream_abort(void *_ctx)
{
    struct ctx *ctx = _ctx;

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
buffer_callback(GString *value, void *_ctx)
{
    struct ctx *ctx = _ctx;

    assert(value != NULL);

    ctx->value = value;
}

static void
test_block1(pool_t pool)
{
    struct ctx ctx = {
        .value = NULL,
        .eof = false,
        .aborted = false,
    };
    istream_t delayed, tee, second;
    struct async_operation_ref async_ref;

    delayed = istream_delayed_new(pool);
    tee = istream_tee_new(pool, delayed, false);
    second = istream_tee_second(tee);

    istream_handler_set(tee, &block_istream_handler, &ctx, 0);

    sink_gstring_new(pool, second, buffer_callback, &ctx, &async_ref);
    assert(ctx.value == NULL);

    /* the input (istream_delayed) blocks */
    istream_read(second);
    assert(ctx.value == NULL);

    /* feed data into input */
    istream_delayed_set(delayed, istream_string_new(pool, "foo"));
    assert(ctx.value == NULL);

    /* the first output (block_istream_handler) blocks */
    istream_read(second);
    assert(ctx.value == NULL);

    /* close the blocking output, this should release the "tee"
       object and restart reading (into the second output) */
    assert(!ctx.aborted && !ctx.eof);
    istream_close(tee);
    assert(ctx.aborted && !ctx.eof);
    assert(ctx.value != NULL);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);
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

    test_block1(root_pool);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();

    event_base_free(event_base);
}
