/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "direct.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "istream/istream.hxx"
#include "istream/Sink.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/istream_cat.hxx"
#include "istream/FailIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/InjectIstream.hxx"
#include "istream/istream_later.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "event/DeferEvent.hxx"

#include <stdexcept>
#include <string>

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

struct Instance : PInstance {
};

struct Context final : IstreamSink {
    using IstreamSink::input;

    Instance &instance;

    bool half = false;
    bool got_data;
    bool eof = false;
#ifdef EXPECTED_RESULT
    bool record = false;
    std::string buffer;
#endif
    InjectIstreamControl *abort_istream = nullptr;
    int abort_after = 0;

    /**
     * An InjectIstream instance which will fail after the data
     * handler has blocked.
     */
    InjectIstreamControl *block_inject = nullptr;

    int block_after = -1;

    bool block_byte = false, block_byte_state = false;

    size_t skipped = 0;

    DeferEvent defer_inject_event;
    InjectIstreamControl *defer_inject_istream = nullptr;
    std::exception_ptr defer_inject_error;

    template<typename I>
    explicit Context(Instance &_instance, I &&_input)
        :IstreamSink(std::forward<I>(_input)), instance(_instance),
         defer_inject_event(instance.event_loop,
                            BIND_THIS_METHOD(DeferredInject)) {}

    void Skip(off_t nbytes) noexcept {
        assert(skipped == 0);
        auto s = input.Skip(nbytes);
        if (s > 0)
            skipped += s;
    }

    int ReadEvent() {
        input.Read();
        return instance.event_loop.LoopOnceNonBlock();
    }

    void ReadExpect() {
        assert(!eof);

        got_data = false;

        gcc_unused
        const auto ret = ReadEvent();
        assert(eof || got_data || ret == 0);

        /* give istream_later another chance to breathe */
        instance.event_loop.LoopOnceNonBlock();
    }

    void DeferInject(InjectIstreamControl &inject, std::exception_ptr ep) {
        assert(ep);
        assert(defer_inject_istream == nullptr);
        assert(!defer_inject_error);

        defer_inject_istream = &inject;
        defer_inject_error = ep;
        defer_inject_event.Schedule();
    }

    void DeferredInject() {
        assert(defer_inject_istream != nullptr);
        assert(defer_inject_error);

        auto &i = *defer_inject_istream;
        defer_inject_istream = nullptr;

        i.InjectFault(std::exchange(defer_inject_error,
                                    std::exception_ptr()));
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
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
                    std::make_exception_ptr(std::runtime_error("block_inject")));
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
                    std::make_exception_ptr(std::runtime_error("abort_istream")));
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

        assert(memcmp((const char *)EXPECTED_RESULT + skipped + buffer.size(), data, length) == 0);

#ifdef __clang__
#pragma GCC diagnostic pop
#endif

        buffer.append((const char *)data, length);
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
                    std::make_exception_ptr(std::runtime_error("block_inject")));
        block_inject = nullptr;
        return 0;
    }

    if (abort_istream != nullptr) {
        DeferInject(*abort_istream,
                    std::make_exception_ptr(std::runtime_error("abort_istream")));
        abort_istream = nullptr;
        return 0;
    }

    return max_length;
}

void
Context::OnEof() noexcept
{
    eof = true;
}

void
Context::OnError(std::exception_ptr) noexcept
{
#ifdef EXPECTED_RESULT
    assert(!record);
#endif

    eof = true;
}

/*
 * utils
 *
 */

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
        ctx.ReadExpect();
#else
    for (int i = 0; i < 1000 && !ctx.eof; ++i)
        ctx.ReadEvent();
#endif

#ifdef EXPECTED_RESULT
    if (ctx.record) {
        assert(ctx.buffer.size() + ctx.skipped == sizeof(EXPECTED_RESULT) - 1);
        assert(memcmp(ctx.buffer.data(),
                      (const char *)EXPECTED_RESULT + ctx.skipped,
                      ctx.buffer.size()) == 0);
    }
#endif

    cleanup();
    pool_commit();
}

template<typename I>
static void
run_istream_block(Instance &instance, struct pool *pool,
                  I &&istream,
                  gcc_unused bool record,
                  int block_after)
{
    Context ctx(instance, std::forward<I>(istream));
    ctx.block_after = block_after;
#ifdef EXPECTED_RESULT
    ctx.record = record;
#endif

    run_istream_ctx(ctx, pool);
}

template<typename I>
static void
run_istream(Instance &instance, struct pool *pool,
            I &&istream, bool record)
{
    run_istream_block(instance, pool, std::forward<I>(istream), record, -1);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_normal", 8192);

    auto istream = create_test(instance.event_loop, *pool, create_input(*pool));
    assert(istream);

    run_istream(instance, pool, std::move(istream), true);
}

/** invoke Istream::Skip(1) */
static void
test_skip(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_skip", 8192);

    auto istream = create_test(instance.event_loop, *pool, create_input(*pool));
    assert(istream);

    Context ctx(instance, std::move(istream));
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif
    ctx.Skip(1);

    run_istream_ctx(ctx, pool);
}

/** block once after n data() invocations */
static void
test_block(Instance &instance)
{
    for (int n = 0; n < 8; ++n) {
        auto *pool = pool_new_linear(instance.root_pool, "test_block", 8192);

        auto istream = create_test(instance.event_loop, *pool,
                                   create_input(*pool));
        assert(istream);

        run_istream_block(instance, pool, std::move(istream), true, n);
    }
}

/** test with istream_byte */
static void
test_byte(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_byte", 8192);

    auto istream =
        create_test(instance.event_loop, *pool,
                    istream_byte_new(*pool, create_input(*pool)));
    run_istream(instance, pool, std::move(istream), true);
}

/** block and consume one byte at a time */
static void
test_block_byte(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_byte", 8192);

    Context ctx(instance,
                create_test(instance.event_loop, *pool,
                            istream_byte_new(*pool, create_input(*pool))));
    ctx.block_byte = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    run_istream_ctx(ctx, pool);
}

/** error occurs while blocking */
static void
test_block_inject(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_block", 8192);

    auto inject = istream_inject_new(*pool, UnusedIstreamPtr(create_input(*pool)));

    Context ctx(instance,
                create_test(instance.event_loop, *pool, std::move(inject.first)));
    ctx.block_inject = &inject.second;
    run_istream_ctx(ctx, pool);

    assert(ctx.eof);
}

/** accept only half of the data */
static void
test_half(Instance &instance)
{
    Context ctx(instance,
                create_test(instance.event_loop, instance.root_pool,
                            create_input(instance.root_pool)));
    ctx.half = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    auto *pool = pool_new_linear(instance.root_pool, "test_half", 8192);

    run_istream_ctx(ctx, pool);
}

/** input fails */
static void
test_fail(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_fail", 8192);

    const std::runtime_error error("test_fail");
    auto istream = create_test(instance.event_loop, *pool,
                               istream_fail_new(*pool, std::make_exception_ptr(error)));
    run_istream(instance, pool, std::move(istream), false);
}

/** input fails after the first byte */
static void
test_fail_1byte(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_fail_1byte", 8192);

    const std::runtime_error error("test_fail");
    auto istream =
        create_test(instance.event_loop, *pool,
                    istream_cat_new(*pool,
                                    istream_head_new(*pool, UnusedIstreamPtr(create_input(*pool)),
                                                     1, false),
                                    istream_fail_new(*pool, std::make_exception_ptr(error))));
    run_istream(instance, pool, std::move(istream), false);
}

/** abort without handler */
static void
test_abort_without_handler(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_abort_without_handler", 8192);

    auto istream = create_test(instance.event_loop, *pool, create_input(*pool));
    pool_unref(pool);
    pool_commit();

    istream.Clear();

    cleanup();
    pool_commit();
}

#ifndef NO_ABORT_ISTREAM

/** abort in handler */
static void
test_abort_in_handler(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_abort_in_handler", 8192);

    auto inject = istream_inject_new(*pool, UnusedIstreamPtr(create_input(*pool)));
    auto istream = create_test(instance.event_loop, *pool, std::move(inject.first));
    pool_unref(pool);
    pool_commit();

    Context ctx(instance, std::move(istream));
    ctx.block_after = -1;
    ctx.abort_istream = &inject.second;

    while (!ctx.eof) {
        ctx.ReadExpect();
        ctx.instance.event_loop.LoopOnceNonBlock();
    }

    assert(ctx.abort_istream == nullptr);

    cleanup();
    pool_commit();
}

/** abort in handler, with some data consumed */
static void
test_abort_in_handler_half(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_abort_in_handler_half", 8192);

    auto inject = istream_inject_new(*pool, istream_four_new(pool, UnusedIstreamPtr(create_input(*pool))));
    auto istream = create_test(instance.event_loop, *pool,
                               istream_byte_new(*pool, std::move(inject.first)));
    pool_unref(pool);
    pool_commit();

    Context ctx(instance, std::move(istream));
    ctx.half = true;
    ctx.abort_after = 2;
    ctx.abort_istream = &inject.second;

    while (!ctx.eof) {
        ctx.ReadExpect();
        ctx.instance.event_loop.LoopOnceNonBlock();
    }

    assert(ctx.abort_istream == nullptr || ctx.abort_after >= 0);

    cleanup();
    pool_commit();
}

#endif

/** abort after 1 byte of output */
static void
test_abort_1byte(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_abort_1byte", 8192);

    auto *istream = istream_head_new(*pool,
                                     UnusedIstreamPtr(create_test(instance.event_loop, *pool,
                                                                  create_input(*pool))),
                                     1, false).Steal();
    run_istream(instance, pool, istream, false);
}

/** test with istream_later filter */
static void
test_later(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_later", 8192);

    auto istream =
        create_test(instance.event_loop, *pool,
                    istream_later_new(*pool, create_input(*pool),
                                      instance.event_loop));
    run_istream(instance, pool, std::move(istream), true);
}

#ifdef EXPECTED_RESULT
/** test with large input and blocking handler */
static void
test_big_hold(Instance &instance)
{
    auto *pool = pool_new_linear(instance.root_pool, "test_big_hold", 8192);

    auto istream = create_input(*pool);
    for (unsigned i = 0; i < 1024; ++i)
        istream = istream_cat_new(*pool, std::move(istream),
                                  create_input(*pool));

    istream = create_test(instance.event_loop, *pool, std::move(istream));
    auto *inner = istream.Steal();
    UnusedHoldIstreamPtr hold(*pool, UnusedIstreamPtr(inner));

    inner->Read();

    hold.Clear();

    pool_unref(pool);
}
#endif

static void
RunTest(void (*f)(Instance &instance))
{
    Instance instance;
    f(instance);
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

    RunTest(test_normal);
    RunTest(test_skip);
    if (enable_blocking) {
        RunTest(test_block);
        RunTest(test_byte);
        RunTest(test_block_byte);
        RunTest(test_block_inject);
    }
    RunTest(test_half);
    RunTest(test_fail);
    RunTest(test_fail_1byte);
    RunTest(test_abort_without_handler);
#ifndef NO_ABORT_ISTREAM
    RunTest(test_abort_in_handler);
    if (enable_blocking)
        RunTest(test_abort_in_handler_half);
#endif
    RunTest(test_abort_1byte);
    RunTest(test_later);

#ifdef EXPECTED_RESULT
    RunTest(test_big_hold);
#endif

#ifdef CUSTOM_TEST
    {
        Instance instance;
        test_custom(instance.event_loop, instance.root_pool);
    }
#endif
}
