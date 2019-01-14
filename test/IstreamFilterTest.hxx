/*
 * Copyright 2007-2019 Content Management AG
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

#pragma once

#include "direct.hxx"
#include "fb_pool.hxx"
#include "PInstance.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/Sink.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/FailIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/InjectIstream.hxx"
#include "istream/istream_later.hxx"
#include "istream/Pointer.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/pool.hxx"
#include "event/DeferEvent.hxx"

#include <gtest/gtest.h>
#include <gtest/gtest-typed-test.h>

#include <stdexcept>
#include <string>

#include <stdio.h>
#include <string.h>

template<typename T>
class IstreamFilterTest : public ::testing::Test {
    const ScopeFbPoolInit fb_pool_init;

public:
    IstreamFilterTest() {
        direct_global_init();
    }
};

TYPED_TEST_CASE_P(IstreamFilterTest);

struct Instance : PInstance {
};

struct Context final : IstreamSink {
    using IstreamSink::input;

    Instance &instance;

    bool half = false;
    bool got_data;
    bool eof = false;

    const char *const expected_result;
    bool record = false;
    std::string buffer;

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
    explicit Context(Instance &_instance, const char *_expected_result,
                     I &&_input) noexcept
        :IstreamSink(std::forward<I>(_input)), instance(_instance),
         expected_result(_expected_result),
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

    void DeferredInject() noexcept {
        assert(defer_inject_istream != nullptr);
        assert(defer_inject_error);

        auto &i = *defer_inject_istream;
        defer_inject_istream = nullptr;

        i.InjectFault(std::exchange(defer_inject_error,
                                    std::exception_ptr()));
    }

    bool ReadBuckets();

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) noexcept override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
    void OnEof() noexcept override;
    void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * utils
 *
 */

template<typename Traits>
static void
run_istream_ctx(const Traits &traits, Context &ctx, struct pool *pool)
{
    ctx.eof = false;

    if (traits.call_available) {
        gcc_unused off_t a1 = ctx.input.GetAvailable(false);
        gcc_unused off_t a2 = ctx.input.GetAvailable(true);
    }

    pool_unref(pool);
    pool_commit();

    if (traits.got_data_assert) {
        while (!ctx.eof)
            ctx.ReadExpect();
    } else {
        for (int i = 0; i < 1000 && !ctx.eof; ++i)
            ctx.ReadEvent();
    }

    if (ctx.expected_result && ctx.record) {
        ASSERT_EQ(ctx.buffer.size() + ctx.skipped,
                  strlen(ctx.expected_result));
        ASSERT_EQ(memcmp(ctx.buffer.data(),
                         (const char *)ctx.expected_result + ctx.skipped,
                         ctx.buffer.size()),
                  0);
    }

    pool_commit();
}

template<typename Traits, typename I>
static void
run_istream_block(const Traits &traits, Instance &instance, struct pool *pool,
                  I &&istream,
                  bool record,
                  int block_after)
{
    Context ctx(instance, traits.expected_result, std::forward<I>(istream));
    ctx.block_after = block_after;
    ctx.record = ctx.expected_result && record;

    run_istream_ctx(traits, ctx, pool);
}

template<typename Traits, typename I>
static void
run_istream(const Traits &traits, Instance &instance, struct pool *pool,
            I &&istream, bool record)
{
    run_istream_block(traits, instance, pool,
                      std::forward<I>(istream), record, -1);
}


/*
 * tests
 *
 */

/** normal run */
TYPED_TEST_P(IstreamFilterTest, Normal)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_normal", 8192);

    auto istream = traits.CreateTest(instance.event_loop, *pool, traits.CreateInput(*pool));
    ASSERT_TRUE(!!istream);

    run_istream(traits, instance, pool, std::move(istream), true);
}

/** test with Istream::FillBucketList() */
TYPED_TEST_P(IstreamFilterTest, Bucket)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_normal", 8192);

    auto istream = traits.CreateTest(instance.event_loop, *pool, traits.CreateInput(*pool));
    ASSERT_TRUE(!!istream);

    Context ctx(instance, traits.expected_result, std::move(istream));
    if (ctx.expected_result)
        ctx.record = true;

    while (ctx.ReadBuckets()) {}

    if (ctx.input.IsDefined())
        run_istream_ctx(traits, ctx, pool);
    else
        pool_unref(pool);
}

/** invoke Istream::Skip(1) */
TYPED_TEST_P(IstreamFilterTest, Skip)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_skip", 8192);

    auto istream = traits.CreateTest(instance.event_loop, *pool, traits.CreateInput(*pool));
    ASSERT_TRUE(!!istream);

    Context ctx(instance, traits.expected_result, std::move(istream));
    ctx.record = ctx.expected_result != nullptr;
    ctx.Skip(1);

    run_istream_ctx(traits, ctx, pool);
}

/** block once after n data() invocations */
TYPED_TEST_P(IstreamFilterTest, Block)
{
    TypeParam traits;
    if (!traits.enable_blocking)
        return;

    Instance instance;

    for (int n = 0; n < 8; ++n) {
        auto *pool = pool_new_linear(instance.root_pool, "test_block", 8192);

        auto istream = traits.CreateTest(instance.event_loop, *pool,
                                   traits.CreateInput(*pool));
        ASSERT_TRUE(!!istream);

        run_istream_block(traits, instance, pool, std::move(istream), true, n);
    }
}

/** test with istream_byte */
TYPED_TEST_P(IstreamFilterTest, Byte)
{
    TypeParam traits;
    if (!traits.enable_blocking)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_byte", 8192);

    auto istream =
        traits.CreateTest(instance.event_loop, *pool,
                          istream_byte_new(*pool, traits.CreateInput(*pool)));
    run_istream(traits, instance, pool, std::move(istream), true);
}

/** block and consume one byte at a time */
TYPED_TEST_P(IstreamFilterTest, BlockByte)
{
    TypeParam traits;
    if (!traits.enable_blocking)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_byte", 8192);

    Context ctx(instance,
                traits.expected_result,
                traits.CreateTest(instance.event_loop, *pool,
                            istream_byte_new(*pool, traits.CreateInput(*pool))));
    ctx.block_byte = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    run_istream_ctx(traits, ctx, pool);
}

/** error occurs while blocking */
TYPED_TEST_P(IstreamFilterTest, BlockInject)
{
    TypeParam traits;
    if (!traits.enable_blocking)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_block", 8192);

    auto inject = istream_inject_new(*pool, traits.CreateInput(*pool));

    Context ctx(instance,
                traits.expected_result,
                traits.CreateTest(instance.event_loop, *pool,
                                  std::move(inject.first)));
    ctx.block_inject = &inject.second;
    run_istream_ctx(traits, ctx, pool);

    ASSERT_TRUE(ctx.eof);
}

/** accept only half of the data */
TYPED_TEST_P(IstreamFilterTest, Half)
{
    TypeParam traits;
    Instance instance;

    Context ctx(instance,
                traits.expected_result,
                traits.CreateTest(instance.event_loop, instance.root_pool,
                                  traits.CreateInput(instance.root_pool)));
    ctx.half = true;
#ifdef EXPECTED_RESULT
    ctx.record = true;
#endif

    auto *pool = pool_new_linear(instance.root_pool, "test_half", 8192);

    run_istream_ctx(traits, ctx, pool);
}

/** input fails */
TYPED_TEST_P(IstreamFilterTest, Fail)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_fail", 8192);

    const std::runtime_error error("test_fail");
    auto istream = traits.CreateTest(instance.event_loop, *pool,
                                     istream_fail_new(*pool, std::make_exception_ptr(error)));
    run_istream(traits, instance, pool, std::move(istream), false);
}

/** input fails after the first byte */
TYPED_TEST_P(IstreamFilterTest, FailAfterFirstByte)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_fail_1byte", 8192);

    const std::runtime_error error("test_fail");
    auto istream =
        traits.CreateTest(instance.event_loop, *pool,
                    istream_cat_new(*pool,
                                    istream_head_new(*pool, traits.CreateInput(*pool),
                                                     1, false),
                                    istream_fail_new(*pool, std::make_exception_ptr(error))));
    run_istream(traits, instance, pool, std::move(istream), false);
}

/** abort without handler */
TYPED_TEST_P(IstreamFilterTest, AbortWithoutHandler)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_abort_without_handler", 8192);

    auto istream = traits.CreateTest(instance.event_loop, *pool, traits.CreateInput(*pool));
    pool_unref(pool);
    pool_commit();

    istream.Clear();

    pool_commit();
}

/** abort in handler */
TYPED_TEST_P(IstreamFilterTest, AbortInHandler)
{
    TypeParam traits;
    if (!traits.enable_abort_istream)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_abort_in_handler", 8192);

    auto inject = istream_inject_new(*pool, traits.CreateInput(*pool));
    auto istream = traits.CreateTest(instance.event_loop, *pool, std::move(inject.first));
    pool_unref(pool);
    pool_commit();

    Context ctx(instance,
                traits.expected_result,
                std::move(istream));
    ctx.block_after = -1;
    ctx.abort_istream = &inject.second;

    while (!ctx.eof) {
        ctx.ReadExpect();
        ctx.instance.event_loop.LoopOnceNonBlock();
    }

    ASSERT_EQ(ctx.abort_istream, nullptr);

    pool_commit();
}

/** abort in handler, with some data consumed */
TYPED_TEST_P(IstreamFilterTest, AbortInHandlerHalf)
{
    TypeParam traits;
    if (!traits.enable_abort_istream || !traits.enable_blocking)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_abort_in_handler_half", 8192);

    auto inject = istream_inject_new(*pool, istream_four_new(pool, traits.CreateInput(*pool)));
    auto istream = traits.CreateTest(instance.event_loop, *pool,
                               istream_byte_new(*pool, std::move(inject.first)));
    pool_unref(pool);
    pool_commit();

    Context ctx(instance,
                traits.expected_result,
                std::move(istream));
    ctx.half = true;
    ctx.abort_after = 2;
    ctx.abort_istream = &inject.second;

    while (!ctx.eof) {
        ctx.ReadExpect();
        ctx.instance.event_loop.LoopOnceNonBlock();
    }

    ASSERT_TRUE(ctx.abort_istream == nullptr || ctx.abort_after >= 0);

    pool_commit();
}

/** abort after 1 byte of output */
TYPED_TEST_P(IstreamFilterTest, AbortAfter1Byte)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_abort_1byte", 8192);

    auto istream = istream_head_new(*pool,
                                    traits.CreateTest(instance.event_loop, *pool,
                                                traits.CreateInput(*pool)),
                                    1, false);
    run_istream(traits, instance, pool, std::move(istream), false);
}

/** test with istream_later filter */
TYPED_TEST_P(IstreamFilterTest, Later)
{
    TypeParam traits;
    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_later", 8192);

    auto istream =
        traits.CreateTest(instance.event_loop, *pool,
                    istream_later_new(*pool, traits.CreateInput(*pool),
                                      instance.event_loop));
    run_istream(traits, instance, pool, std::move(istream), true);
}

/** test with large input and blocking handler */
TYPED_TEST_P(IstreamFilterTest, BigHold)
{
    TypeParam traits;
    if (!traits.expected_result)
        return;

    Instance instance;

    auto *pool = pool_new_linear(instance.root_pool, "test_big_hold", 8192);

    auto istream = traits.CreateInput(*pool);
    for (unsigned i = 0; i < 1024; ++i)
        istream = istream_cat_new(*pool, std::move(istream),
                                  traits.CreateInput(*pool));

    istream = traits.CreateTest(instance.event_loop, *pool, std::move(istream));
    auto *inner = istream.Steal();
    UnusedHoldIstreamPtr hold(*pool, UnusedIstreamPtr(inner));

    inner->Read();

    hold.Clear();

    pool_unref(pool);

    pool_commit();
}

REGISTER_TYPED_TEST_CASE_P(IstreamFilterTest,
                           Normal,
                           Bucket,
                           Skip,
                           Block,
                           Byte,
                           BlockByte,
                           BlockInject,
                           Half,
                           Fail,
                           FailAfterFirstByte,
                           AbortWithoutHandler,
                           AbortInHandler,
                           AbortInHandlerHalf,
                           AbortAfter1Byte,
                           Later,
                           BigHold);
