// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "memory/fb_pool.hxx"
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
#include "istream/istream_null.hxx"
#include "istream/istream_later.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "pool/pool.hxx"
#include "io/SpliceSupport.hxx"
#include "event/DeferEvent.hxx"

#include <gtest/gtest.h>
#include <gtest/gtest-typed-test.h>

#include <stdexcept>
#include <string>
#include <type_traits>

#include <stdio.h>
#include <string.h>

class AutoPoolCommit {
public:
	~AutoPoolCommit() noexcept {
		pool_commit();
	}
};

template<typename T>
class IstreamFilterTest : public ::testing::Test {
	const ScopeFbPoolInit fb_pool_init;

public:
	IstreamFilterTest() noexcept {
		direct_global_init();
	}
};

TYPED_TEST_CASE_P(IstreamFilterTest);

struct Instance : AutoPoolCommit, PInstance {
};

struct Context final : IstreamSink {
	using IstreamSink::input;

	Instance &instance;

	PoolPtr test_pool;

	bool half = false;
	bool got_data;
	bool eof = false;

	/**
	 * Call EventLoop::Break() as soon as the stream ends?
	 */
	bool break_eof = false;

	int close_after = -1;

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

	/**
	 * The current offset in the #Istream.
	 */
	std::size_t offset = 0;

	std::size_t skipped = 0;

	DeferEvent defer_inject_event;
	InjectIstreamControl *defer_inject_istream = nullptr;
	std::exception_ptr defer_inject_error;

	template<typename I>
	explicit Context(Instance &_instance, PoolPtr &&_test_pool,
			 const char *_expected_result,
			 I &&_input) noexcept
		:IstreamSink(std::forward<I>(_input)), instance(_instance),
		 test_pool(std::move(_test_pool)),
		 expected_result(_expected_result),
		 defer_inject_event(instance.event_loop,
				    BIND_THIS_METHOD(DeferredInject))
	{
		assert(test_pool);
	}

	using IstreamSink::CloseInput;

	void Skip(off_t nbytes) noexcept {
		assert(skipped == 0);
		auto s = input.Skip(nbytes);
		if (s > 0) {
			skipped += s;
			offset += s;
		}
	}

	void DeferInject(InjectIstreamControl &inject,
			 std::exception_ptr ep) noexcept {
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

	bool ReadBuckets(std::size_t limit);

	void WaitForEndOfStream() noexcept {
		assert(!break_eof);
		break_eof = true;

		while (!eof) {
			if (HasInput())
				input.Read();

			if (!eof)
				instance.event_loop.Run();
		}

		break_eof = false;

		assert(!HasInput());
		assert(eof);
	}

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

/*
 * utils
 *
 */

template<typename Traits>
static void
run_istream_ctx(const Traits &traits, Context &ctx) noexcept
{
	const AutoPoolCommit auto_pool_commit;

	ctx.eof = false;

	if (traits.call_available) {
		[[maybe_unused]] off_t a1 = ctx.input.GetAvailable(false);
		[[maybe_unused]] off_t a2 = ctx.input.GetAvailable(true);
	}

	ctx.WaitForEndOfStream();

	if (ctx.expected_result && ctx.record) {
		ASSERT_EQ(ctx.buffer.size() + ctx.skipped,
			  strlen(ctx.expected_result));
		ASSERT_EQ(memcmp(ctx.buffer.data(),
				 (const char *)ctx.expected_result + ctx.skipped,
				 ctx.buffer.size()),
			  0);
	}
}

template<typename Traits, typename I>
static void
run_istream_block(const Traits &traits, Instance &instance, PoolPtr pool,
		  I &&istream,
		  bool record,
		  int block_after)
{
	Context ctx(instance, std::move(pool),
		    traits.expected_result, std::forward<I>(istream));
	ctx.block_after = block_after;
	ctx.record = ctx.expected_result && record;

	run_istream_ctx(traits, ctx);
}

template<typename Traits, typename I>
static void
run_istream(const Traits &traits, Instance &instance, PoolPtr pool,
	    I &&istream, bool record)
{
	run_istream_block(traits, instance, std::move(pool),
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

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool, traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), true);
}

/** test with Istream::FillBucketList() */
TYPED_TEST_P(IstreamFilterTest, Bucket)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.expected_result, std::move(istream));
	if (ctx.expected_result)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(traits, ctx);
}

/** test with Istream::FillBucketList() */
TYPED_TEST_P(IstreamFilterTest, SmallBucket)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.expected_result, std::move(istream));
	if (ctx.expected_result)
		ctx.record = true;

	while (ctx.ReadBuckets(3)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(traits, ctx);
}

/** Istream::FillBucketList() throws */
TYPED_TEST_P(IstreamFilterTest, BucketError)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	const std::runtime_error error("test_fail");
	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_fail_new(pool, std::make_exception_ptr(error)));
	ASSERT_TRUE(!!istream);

	Context ctx(instance, std::move(pool),
		    traits.expected_result, std::move(istream));
	if (ctx.expected_result)
		ctx.record = true;

	try {
		while (ctx.ReadBuckets(3)) {}

		/* this is only reachable if the Istream doesn't support
		   FillBucketList() */
		ASSERT_TRUE(ctx.input.IsDefined());
		ctx.CloseInput();
	} catch (...) {
		ASSERT_FALSE(ctx.input.IsDefined());
	}
}

/** invoke Istream::Skip(1) */
TYPED_TEST_P(IstreamFilterTest, Skip)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.expected_result, std::move(istream));
	ctx.record = ctx.expected_result != nullptr;
	ctx.Skip(1);

	run_istream_ctx(traits, ctx);
}

/** block once after n data() invocations */
TYPED_TEST_P(IstreamFilterTest, Block)
{
	TypeParam traits;
	if (!traits.enable_blocking)
		return;

	Instance instance;

	for (int n = 0; n < 8; ++n) {
		auto pool = pool_new_linear(instance.root_pool, "test", 8192);
		auto input_pool = pool_new_linear(instance.root_pool, "input",
						  8192);

		auto istream = traits.CreateTest(instance.event_loop, pool,
						 traits.CreateInput(input_pool));
		ASSERT_TRUE(!!istream);
		input_pool.reset();

		run_istream_block(traits, instance, std::move(pool),
				  std::move(istream), true, n);
	}
}

/** test with istream_byte */
TYPED_TEST_P(IstreamFilterTest, Byte)
{
	TypeParam traits;
	if (!traits.enable_blocking)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  istream_byte_new(input_pool,
						   traits.CreateInput(input_pool)));
	input_pool.reset();

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), true);
}

/** block and consume one byte at a time */
TYPED_TEST_P(IstreamFilterTest, BlockByte)
{
	TypeParam traits;
	if (!traits.enable_blocking)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_byte_new(input_pool,
							  traits.CreateInput(input_pool)));
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.block_byte = true;
#ifdef EXPECTED_RESULT
	ctx.record = true;
#endif

	run_istream_ctx(traits, ctx);
}

/** error occurs while blocking */
TYPED_TEST_P(IstreamFilterTest, BlockInject)
{
	TypeParam traits;
	if (!traits.enable_blocking)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool,
					 traits.CreateInput(input_pool));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 std::move(inject.first));

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.block_inject = &inject.second;

	run_istream_ctx(traits, ctx);

	ASSERT_TRUE(ctx.eof);
}

/** accept only half of the data */
TYPED_TEST_P(IstreamFilterTest, Half)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.half = true;
#ifdef EXPECTED_RESULT
	ctx.record = true;
#endif

	run_istream_ctx(traits, ctx);
}

/** input fails */
TYPED_TEST_P(IstreamFilterTest, Fail)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	const std::runtime_error error("test_fail");
	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_fail_new(pool, std::make_exception_ptr(error)));

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), false);
}

/** input fails after the first byte */
TYPED_TEST_P(IstreamFilterTest, FailAfterFirstByte)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	const std::runtime_error error("test_fail");
	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  NewConcatIstream(input_pool,
						   istream_head_new(input_pool,
								    traits.CreateInput(input_pool),
								    1, false),
						   istream_fail_new(input_pool,
								    std::make_exception_ptr(error))));
	input_pool.reset();

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), false);
}

TYPED_TEST_P(IstreamFilterTest, CloseInHandler)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto input = traits.CreateInput(input_pool);
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool, std::move(input));

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.close_after = 0;

	run_istream_ctx(traits, ctx);
}

/** abort without handler */
TYPED_TEST_P(IstreamFilterTest, AbortWithoutHandler)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	input_pool.reset();

	istream.Clear();
}

/** abort in handler */
TYPED_TEST_P(IstreamFilterTest, AbortInHandler)
{
	TypeParam traits;
	if (!traits.enable_abort_istream)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool, traits.CreateInput(input_pool));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool, std::move(inject.first));

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.block_after = -1;
	ctx.abort_istream = &inject.second;

	ctx.WaitForEndOfStream();

	ASSERT_EQ(ctx.abort_istream, nullptr);
}

/** abort in handler, with some data consumed */
TYPED_TEST_P(IstreamFilterTest, AbortInHandlerHalf)
{
	TypeParam traits;
	if (!traits.enable_abort_istream || !traits.enable_blocking)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool,
					 istream_four_new(input_pool,
							  traits.CreateInput(input_pool)));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_byte_new(pool, std::move(inject.first)));

	Context ctx(instance, std::move(pool),
		    traits.expected_result,
		    std::move(istream));
	ctx.half = true;
	ctx.abort_after = 2;
	ctx.abort_istream = &inject.second;

	ctx.WaitForEndOfStream();

	ASSERT_TRUE(ctx.abort_istream == nullptr || ctx.abort_after >= 0);
}

/** abort after 1 byte of output */
TYPED_TEST_P(IstreamFilterTest, AbortAfter1Byte)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = istream_head_new(pool,
					traits.CreateTest(instance.event_loop, pool,
							  traits.CreateInput(input_pool)),
					1, false);
	input_pool.reset();

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), false);
}

/** test with istream_later filter */
TYPED_TEST_P(IstreamFilterTest, Later)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  istream_later_new(input_pool, traits.CreateInput(input_pool),
						    instance.event_loop));
	input_pool.reset();

	run_istream(traits, instance, std::move(pool),
		    std::move(istream), true);
}

/** call Istream::AsFd() */
TYPED_TEST_P(IstreamFilterTest, AsFd)
{
	TypeParam traits;
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  istream_null_new(pool));

	int fd = istream.AsFd();
	if (fd >= 0) {
		EXPECT_FALSE(istream);
	} else {
		EXPECT_TRUE(istream);
	}
}

/** test with large input and blocking handler */
TYPED_TEST_P(IstreamFilterTest, BigHold)
{
#ifndef ISTREAM_TEST_NO_BIG
	TypeParam traits;
	if (!traits.expected_result)
		return;

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateInput(input_pool);
	for (unsigned i = 0; i < 1024; ++i)
		istream = NewConcatIstream(input_pool, std::move(istream),
					   traits.CreateInput(input_pool));
	input_pool.reset();

	istream = traits.CreateTest(instance.event_loop, pool, std::move(istream));
	auto *inner = istream.Steal();
	UnusedHoldIstreamPtr hold(pool, UnusedIstreamPtr(inner));

	inner->Read();

	hold.Clear();
#endif
}

REGISTER_TYPED_TEST_CASE_P(IstreamFilterTest,
			   Normal,
			   Bucket,
			   SmallBucket,
			   BucketError,
			   Skip,
			   Block,
			   Byte,
			   BlockByte,
			   BlockInject,
			   Half,
			   Fail,
			   FailAfterFirstByte,
			   CloseInHandler,
			   AbortWithoutHandler,
			   AbortInHandler,
			   AbortInHandlerHalf,
			   AbortAfter1Byte,
			   AsFd,
			   Later,
			   BigHold);
