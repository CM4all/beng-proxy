#include "direct.hxx"
#include "fb_pool.hxx"
#include "RootPool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_cat.hxx"
#include "istream/istream_fail.hxx"
#include "istream/istream_four.hxx"
#include "istream/istream_head.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_inject.hxx"
#include "istream/istream_later.hxx"
#include "istream/istream_pointer.hxx"
#include "istream/istream_oo.hxx"
#include "event/Event.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"

#include <glib.h>

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

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

#ifndef FILTER_CLEANUP
static void
cleanup(void)
{
}
#endif

struct Context final : IstreamHandler {
    IstreamPointer input;

    bool half = false;
    bool got_data;
    bool eof = false;
#ifdef EXPECTED_RESULT
    bool record = false;
    char buffer[sizeof(EXPECTED_RESULT) * 2];
    size_t buffer_length = 0;
#endif
    Istream *abort_istream = nullptr;
    int abort_after = 0;

    /**
     * An InjectIstream instance which will fail after the data
     * handler has blocked.
     */
    Istream *block_inject = nullptr;

    int block_after = -1;

    bool block_byte = false, block_byte_state = false;

    size_t skipped = 0;

    DeferEvent defer_inject_event;
    Istream *defer_inject_istream = nullptr;
    GError *defer_inject_error = nullptr;

    explicit Context(Istream &_input)
        :input(_input, *this),
         defer_inject_event(MakeSimpleEventCallback(Context,
                                                    DeferredInject),
                            this) {}

    ~Context() {
        if (defer_inject_error != nullptr)
            g_error_free(defer_inject_error);
    }

    void DeferInject(Istream &istream, GError *error) {
        assert(error != nullptr);
        assert(defer_inject_istream == nullptr);
        assert(defer_inject_error == nullptr);

        defer_inject_istream = &istream;
        defer_inject_error = error;
        defer_inject_event.Add();
    }

    void DeferredInject() {
        assert(defer_inject_istream != nullptr);
        assert(defer_inject_error != nullptr);

        auto &i = *defer_inject_istream;
        defer_inject_istream = nullptr;
        auto e = defer_inject_error;
        defer_inject_error = nullptr;

        istream_inject_fault(i, e);
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
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

    if (block_inject != nullptr) {
        DeferInject(*block_inject,
                    g_error_new_literal(test_quark(), 0, "block_inject"));
        block_inject = nullptr;
        return 0;
    }

    if (block_byte) {
        block_byte_state = !block_byte_state;
        if (block_byte_state)
            return 0;
    }

    if (abort_istream != nullptr && abort_after-- == 0) {
        DeferInject(*abort_istream,
                    g_error_new_literal(test_quark(), 0, "abort_istream"));
        abort_istream = nullptr;
        return 0;
    }

    if (half && length > 8)
        length = (length + 1) / 2;

    if (block_after >= 0) {
        --block_after;
        if (block_after == -1)
            /* block once */
            return 0;
    }

#ifdef EXPECTED_RESULT
    if (record) {
#ifdef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstring-plus-int"
#endif

        assert(buffer_length + skipped + length < sizeof(buffer));
        assert(memcmp(EXPECTED_RESULT + skipped + buffer_length, data, length) == 0);

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

        if (buffer_length + length < sizeof(buffer))
            memcpy(buffer + buffer_length, data, length);
        buffer_length += length;
    }
#endif

    return length;
}

ssize_t
Context::OnDirect(gcc_unused FdType type, gcc_unused int fd, size_t max_length)
{
    got_data = true;

    if (block_inject != nullptr) {
        DeferInject(*block_inject,
                    g_error_new_literal(test_quark(), 0, "block_inject"));
        block_inject = nullptr;
        return 0;
    }

    if (abort_istream != nullptr) {
        DeferInject(*abort_istream,
                    g_error_new_literal(test_quark(), 0, "abort_istream"));
        abort_istream = nullptr;
        return 0;
    }

    return max_length;
}

void
Context::OnEof()
{
    eof = true;
}

void
Context::OnError(GError *error)
{
    g_error_free(error);

#ifdef EXPECTED_RESULT
    assert(!record);
#endif

    eof = true;
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

static inline void
istream_read_expect(Context &ctx)
{
    assert(!ctx.eof);

    ctx.got_data = false;

    const auto ret = istream_read_event(ctx.input);
    assert(ctx.eof || ctx.got_data || ret == 0);

    /* give istream_later another chance to breathe */
    event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
}

static void
run_istream_ctx(Context &ctx, struct pool *pool)
{
    ctx.eof = false;

#ifndef NO_AVAILABLE_CALL
    gcc_unused off_t a1 = ctx.input.GetAvailable(false);
    gcc_unused off_t a2 = ctx.input.GetAvailable(true);
#endif

    pool_unref(pool);
    pool_commit();

#ifndef NO_GOT_DATA_ASSERT
    while (!ctx.eof)
        istream_read_expect(ctx);
#else
    for (int i = 0; i < 1000 && !ctx.eof; ++i)
           istream_read_event(ctx.input);
#endif

#ifdef EXPECTED_RESULT
    if (ctx.record) {
        assert(ctx.buffer_length + ctx.skipped == sizeof(EXPECTED_RESULT) - 1);
        assert(memcmp(ctx.buffer, EXPECTED_RESULT + ctx.skipped,
                      ctx.buffer_length - ctx.skipped) == 0);
    }
#endif

    cleanup();
    pool_commit();
}

static void
run_istream_block(struct pool *pool, Istream *istream,
                  gcc_unused bool record,
                  int block_after)
{
    Context ctx(*istream);
    ctx.block_after = block_after;
#ifdef EXPECTED_RESULT
    ctx.record = record;
#endif

    run_istream_ctx(ctx, pool);
}

static void
run_istream(struct pool *pool, Istream *istream, bool record)
{
    run_istream_block(pool, istream, record, -1);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_normal", 8192);

    auto *istream = create_test(pool, create_input(pool));
    assert(istream != nullptr);
    assert(!istream->HasHandler());

    run_istream(pool, istream, true);
}

/** invoke Istream::Skip(1) */
static void
test_skip(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_skip", 8192);

    auto *istream = create_test(pool, create_input(pool));
    assert(istream != nullptr);
    assert(!istream->HasHandler());

    off_t skipped = istream->Skip(1);

    Context ctx(*istream);
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif
    if (skipped > 0)
        ctx.skipped = skipped;

    run_istream_ctx(ctx, pool);
}

/** block once after n data() invocations */
static void
test_block(struct pool *parent_pool)
{
    for (int n = 0; n < 8; ++n) {
        Istream *istream;

        auto *pool = pool_new_linear(parent_pool, "test_block", 8192);

        istream = create_test(pool, create_input(pool));
        assert(istream != nullptr);
        assert(!istream->HasHandler());

        run_istream_block(pool, istream, true, n);
    }
}

/** test with istream_byte */
static void
test_byte(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_byte", 8192);

    auto *istream =
        create_test(pool, istream_byte_new(*pool, *create_input(pool)));
    run_istream(pool, istream, true);
}

/** block and consume one byte at a time */
static void
test_block_byte(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_byte", 8192);

    Context ctx(*create_test(pool,
                             istream_byte_new(*pool, *create_input(pool))));
    ctx.block_byte = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    run_istream_ctx(ctx, pool);
}

/** error occurs while blocking */
static void
test_block_inject(struct pool *parent_pool)
{
    auto *pool = pool_new_linear(parent_pool, "test_block", 8192);

    auto *inject = istream_inject_new(*pool, *create_input(pool));

    Context ctx(*create_test(pool, inject));
    ctx.block_inject = inject;
    run_istream_ctx(ctx, pool);

    assert(ctx.eof);
}

/** accept only half of the data */
static void
test_half(struct pool *pool)
{
    Context ctx(*create_test(pool, create_input(pool)));
    ctx.half = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    pool = pool_new_linear(pool, "test_half", 8192);

    run_istream_ctx(ctx, pool);
}

/** input fails */
static void
test_fail(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_fail", 8192);

    GError *error = g_error_new_literal(test_quark(), 0, "test_fail");
    auto *istream = create_test(pool, istream_fail_new(pool, error));
    run_istream(pool, istream, false);
}

/** input fails after the first byte */
static void
test_fail_1byte(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_fail_1byte", 8192);

    GError *error = g_error_new_literal(test_quark(), 0, "test_fail");
    auto *istream =
        create_test(pool,
                    istream_cat_new(*pool,
                                    istream_head_new(pool, *create_input(pool),
                                                     1, false),
                                    istream_fail_new(pool, error)));
    run_istream(pool, istream, false);
}

/** abort without handler */
static void
test_abort_without_handler(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_abort_without_handler", 8192);

    auto *istream = create_test(pool, create_input(pool));
    pool_unref(pool);
    pool_commit();

    istream->CloseUnused();

    cleanup();
    pool_commit();
}

#ifndef NO_ABORT_ISTREAM

/** abort in handler */
static void
test_abort_in_handler(struct pool *pool)
{

    pool = pool_new_linear(pool, "test_abort_in_handler", 8192);

    auto *abort_istream = istream_inject_new(*pool, *create_input(pool));
    auto *istream = create_test(pool, abort_istream);
    pool_unref(pool);
    pool_commit();

    Context ctx(*istream);
    ctx.block_after = -1;
    ctx.abort_istream = abort_istream;

    while (!ctx.eof) {
        istream_read_expect(ctx);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == nullptr);

    cleanup();
    pool_commit();
}

/** abort in handler, with some data consumed */
static void
test_abort_in_handler_half(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_abort_in_handler_half", 8192);

    auto *abort_istream =
        istream_inject_new(*pool, *istream_four_new(pool, *create_input(pool)));
    auto *istream = create_test(pool, istream_byte_new(*pool, *abort_istream));
    pool_unref(pool);
    pool_commit();

    Context ctx(*istream);
    ctx.half = true;
    ctx.abort_after = 2;
    ctx.abort_istream = abort_istream;

    while (!ctx.eof) {
        istream_read_expect(ctx);
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    assert(ctx.abort_istream == nullptr || ctx.abort_after >= 0);

    cleanup();
    pool_commit();
}

#endif

/** abort after 1 byte of output */
static void
test_abort_1byte(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_abort_1byte", 8192);

    auto *istream = istream_head_new(pool,
                                     *create_test(pool,
                                                  create_input(pool)),
                                     1, false);
    run_istream(pool, istream, false);
}

/** test with istream_later filter */
static void
test_later(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_later", 8192);

    auto *istream =
        create_test(pool, istream_later_new(pool, *create_input(pool)));
    run_istream(pool, istream, true);
}

#ifdef EXPECTED_RESULT
/** test with large input and blocking handler */
static void
test_big_hold(struct pool *pool)
{
    pool = pool_new_linear(pool, "test_big_hold", 8192);

    Istream *istream = create_input(pool);
    for (unsigned i = 0; i < 1024; ++i)
        istream = istream_cat_new(*pool, istream, create_input(pool));

    istream = create_test(pool, istream);
    Istream *hold = istream_hold_new(*pool, *istream);

    istream->Read();

    hold->CloseUnused();

    pool_unref(pool);
}
#endif


/*
 * main
 *
 */


int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    EventBase event_base;

    direct_global_init();
    fb_pool_init(false);

    /* run test suite */

    test_normal(RootPool());
    test_skip(RootPool());
    if (enable_blocking) {
        test_block(RootPool());
        test_byte(RootPool());
        test_block_byte(RootPool());
        test_block_inject(RootPool());
    }
    test_half(RootPool());
    test_fail(RootPool());
    test_fail_1byte(RootPool());
    test_abort_without_handler(RootPool());
#ifndef NO_ABORT_ISTREAM
    test_abort_in_handler(RootPool());
    if (enable_blocking)
        test_abort_in_handler_half(RootPool());
#endif
    test_abort_1byte(RootPool());
    test_later(RootPool());

#ifdef EXPECTED_RESULT
    test_big_hold(RootPool());
#endif

#ifdef CUSTOM_TEST
    test_custom(RootPool());
#endif

    /* cleanup */

    fb_pool_deinit();
}
