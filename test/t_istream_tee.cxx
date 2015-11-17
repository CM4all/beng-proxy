#include "istream/istream_tee.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream.hxx"
#include "istream/sink_close.hxx"
#include "istream/sink_gstring.hxx"
#include "async.hxx"
#include "event/Event.hxx"

#include <glib.h>

#include <string.h>

struct StatsIstreamHandler : IstreamHandler {
    size_t total_data = 0;
    bool eof = false;
    GError *error = nullptr;

    ~StatsIstreamHandler() {
        if (error != nullptr)
            g_error_free(error);
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(gcc_unused const void *data, size_t length) override {
        total_data += length;
        return length;
    }

    void OnEof() override {
        eof = true;
    }

    void OnError(GError *_error) override {
        error = _error;
    }
};

struct Context {
    GString *value = nullptr;
};

struct BlockContext final : Context, StatsIstreamHandler {
    /* istream handler */

    size_t OnData(gcc_unused const void *data, gcc_unused size_t length) override {
        // block
        return 0;
    }
};

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

/*
 * tests
 *
 */

static void
buffer_callback(GString *value, GError *error, void *_ctx)
{
    auto *ctx = (Context *)_ctx;

    assert(value != nullptr);
    assert(error == nullptr);

    ctx->value = value;
}

static void
test_block1(struct pool *pool)
{
    BlockContext ctx;
    struct async_operation_ref async_ref;

    pool = pool_new_libc(nullptr, "test");

    Istream *delayed = istream_delayed_new(pool);
    Istream *tee = istream_tee_new(*pool, *delayed, false, false);
    Istream *second = &istream_tee_second(*tee);

    tee->SetHandler(ctx);

    sink_gstring_new(*pool, *second, buffer_callback, (Context *)&ctx, async_ref);
    assert(ctx.value == nullptr);

    pool_unref(pool);

    /* the input (istream_delayed) blocks */
    second->Read();
    assert(ctx.value == nullptr);

    /* feed data into input */
    istream_delayed_set(*delayed, *istream_string_new(pool, "foo"));
    assert(ctx.value == nullptr);

    /* the first output (block_istream_handler) blocks */
    second->Read();
    assert(ctx.value == nullptr);

    /* close the blocking output, this should release the "tee"
       object and restart reading (into the second output) */
    assert(ctx.error == nullptr && !ctx.eof);
    istream_free(&tee);
    assert(ctx.error == nullptr && !ctx.eof);
    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);

    pool_commit();
}

static void
test_close_data(struct pool *pool)
{
    Context ctx;
    struct async_operation_ref async_ref;

    pool = pool_new_libc(nullptr, "test");
    Istream *tee =
        istream_tee_new(*pool, *istream_string_new(pool, "foo"), false, false);

    sink_close_new(*pool, *tee);
    Istream *second = &istream_tee_second(*tee);

    sink_gstring_new(*pool, *second, buffer_callback, &ctx, async_ref);
    assert(ctx.value == nullptr);

    pool_unref(pool);

    second->Read();

    /* at this point, sink_close has closed itself, and istream_tee
       should have passed the data to the sink_gstring */

    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);

    pool_commit();
}

/**
 * Close the second output after data has been consumed only by the
 * first output.  This verifies that istream_tee's "skip" attribute is
 * obeyed properly.
 */
static void
test_close_skipped(struct pool *pool)
{
    Context ctx;
    struct async_operation_ref async_ref;

    pool = pool_new_libc(nullptr, "test");
    Istream *input = istream_string_new(pool, "foo");
    Istream *tee = istream_tee_new(*pool, *input, false, false);
    sink_gstring_new(*pool, *tee, buffer_callback, &ctx, async_ref);

    Istream *second = &istream_tee_second(*tee);
    sink_close_new(*pool, *second);
    pool_unref(pool);

    assert(ctx.value == nullptr);

    input->Read();

    assert(ctx.value != nullptr);
    assert(strcmp(ctx.value->str, "foo") == 0);
    g_string_free(ctx.value, true);

    pool_commit();
}

static void
test_error(struct pool *pool, bool close_first, bool close_second,
           bool read_first)
{
    pool = pool_new_libc(nullptr, "test");
    Istream *tee =
        istream_tee_new(*pool, *istream_fail_new(pool,
                                                 g_error_new_literal(test_quark(), 0, "error")),
                        false, false);
    pool_unref(pool);

    StatsIstreamHandler first;
    if (close_first)
        tee->Close();
    else
        tee->SetHandler(first);

    StatsIstreamHandler second;
    auto &tee2 = istream_tee_second(*tee);
    if (close_second)
        tee2.Close();
    else
        tee2.SetHandler(second);

    if (first.error == nullptr && first.error == nullptr) {
        if (read_first)
            tee->Read();
        else
            tee2.Read();
    }

    if (!close_first) {
        assert(first.total_data == 0);
        assert(!first.eof);
        assert(first.error != nullptr);
    }

    if (!close_second) {
        assert(second.total_data == 0);
        assert(!second.eof);
        assert(second.error != nullptr);
    }

    pool_commit();
}

/*
 * main
 *
 */


int main(int argc, char **argv) {
    struct pool *root_pool;

    (void)argc;
    (void)argv;

    EventBase event_base;
    root_pool = pool_new_libc(nullptr, "root");

    /* run test suite */

    test_block1(root_pool);
    test_close_data(root_pool);
    test_close_skipped(root_pool);
    test_error(root_pool, false, false, true);
    test_error(root_pool, false, false, false);
    test_error(root_pool, true, false, false);
    test_error(root_pool, false, true, true);

    /* cleanup */

    pool_unref(root_pool);
    pool_commit();

    pool_recycler_clear();
}
