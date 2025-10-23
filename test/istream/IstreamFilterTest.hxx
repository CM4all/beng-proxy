// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "../TestInstance.hxx"
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
#include "istream/HalfSuspendIstream.hxx"
#include "istream/ReadyIstream.hxx"
#include "istream/SecondFailIstream.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "istream/ForwardIstream.hxx"
#include "istream/New.hxx"
#include "pool/pool.hxx"
#include "event/DeferEvent.hxx"

#include <gtest/gtest.h>
#include <gtest/gtest-typed-test.h>

#include <stdexcept>
#include <string>
#include <type_traits>

#include <stdio.h>
#include <string.h>

struct IstreamFilterTestOptions {
	std::string_view expected_result{};

	using TransformResult = std::string (*)(std::string_view src);
	TransformResult transform_result = nullptr;

	bool call_available = true;
	bool enable_blocking = true;
	bool enable_abort_istream = true;
	bool enable_big = true;

	/**
	 * Enable tests that transfer byte-by-byte?  This should be
	 * disabled for tests with huge inputs because they would take
	 * too long.
	 */
	bool enable_byte = true;

	/**
	 * If disabled, all bucket tests are skipped.
	 */
	bool enable_buckets = true;

	/**
	 * Enable or disable BucketSecondFail.  Some implementations
	 * cannot properly handle this because they do a lot of
	 * buffering and never do a second FillBucketList() call.
	 */
	bool enable_buckets_second_fail = true;

	/**
	 * Does the #Istream implementation forward errors from the
	 * input?  (e.g. #CatchIstream does not)
	 */
	bool forwards_errors = true;

	/**
	 * If true, then the Istream implementation can buffer a lot
	 * and finish late, so OnData() gets called only after input
	 * EOF is reached.  This requires relaxing a few tests.
	 */
	bool late_finish = false;
};

struct Instance : TestInstance {
};

template<typename T>
class IstreamFilterTest : public ::testing::Test {
protected:
	Instance instance_;
	T traits_;
};

TYPED_TEST_SUITE_P(IstreamFilterTest);

struct Context final : IstreamSink {
	using IstreamSink::input;

	Instance &instance;

	PoolPtr test_pool;

	const IstreamFilterTestOptions options;

	bool half = false;
	bool got_data;
	bool eof = false, error = false;

	/**
	 * Call input.GetAvailable() before/after input.FillBucketList()?
	 */
	bool get_available_before_bucket = true, get_available_after_bucket = true;

	bool fill_buckets_twice = false;

	bool bucket_fallback = false, bucket_eof = false;

	/**
	 * Call EventLoop::Break() as soon as the #Istream becomes
	 * ready?
	 */
	bool break_ready = false;

	/**
	 * Call EventLoop::Break() as soon as the stream ends?
	 */
	bool break_eof = false;

	/**
	 * Should OnIstreamReady() try to read buckets?
	 */
	bool on_ready_buckets = false;

	/**
	 * OnReady() does not close #Istream on end-of-file, but
         * instead returns IstreamReadyResult::OK.
	 */
	bool ready_eof_ok = false;

	int close_after = -1;

	bool record = false;
	std::string buffer;

	std::shared_ptr<InjectIstreamControl> abort_istream;
	int abort_after = 0;

	/**
	 * An InjectIstream instance which will fail after the data
	 * handler has blocked.
	 */
	std::shared_ptr<InjectIstreamControl> block_inject;

	int block_after = -1;

	bool block_byte = false, block_byte_state = false;

	/**
	 * The current offset in the #Istream.
	 */
	std::size_t offset = 0;

	std::size_t skipped = 0;

	DeferEvent defer_inject_event;
	std::shared_ptr<InjectIstreamControl> defer_inject_istream;
	std::exception_ptr defer_inject_error;

	template<typename I>
	explicit Context(Instance &_instance, PoolPtr &&_test_pool,
			 const IstreamFilterTestOptions &_options,
			 I &&_input) noexcept
		:IstreamSink(std::forward<I>(_input)), instance(_instance),
		 test_pool(std::move(_test_pool)),
		 options(_options),
		 defer_inject_event(instance.event_loop,
				    BIND_THIS_METHOD(DeferredInject))
	{
		assert(test_pool);
	}

	~Context() noexcept {
		if (HasInput())
			CloseInput();
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

	void DeferInject(std::shared_ptr<InjectIstreamControl> &&inject,
			 std::exception_ptr ep) noexcept;

	void DeferredInject() noexcept;

	std::pair<IstreamReadyResult, bool> ReadBuckets2(std::size_t limit, bool consume_more);
	bool ReadBuckets(std::size_t limit, bool consume_more=false);
	bool ReadBucketsOrFallback(std::size_t limit, bool consume_more=false);

	void WaitForEndOfStream() noexcept;

	/* virtual methods from class IstreamHandler */
	IstreamReadyResult OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr &&ep) noexcept override;

private:
	bool HandleBlockInject() noexcept;
};

void
run_istream_ctx(Context &ctx);

void
run_istream_block(const IstreamFilterTestOptions &options,
		  Instance &instance, PoolPtr pool,
		  UnusedIstreamPtr istream,
		  bool record,
		  int block_after);

void
run_istream(const IstreamFilterTestOptions &options,
	    Instance &instance, PoolPtr pool,
	    UnusedIstreamPtr istream, bool record);

/*
 * tests
 *
 */

/** normal run */
TYPED_TEST_P(IstreamFilterTest, Normal)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool, traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	run_istream(traits.options, instance, std::move(pool),
		    std::move(istream), true);
}

/** suspend the first half of the input */
TYPED_TEST_P(IstreamFilterTest, HalfSuspend)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 NewHalfSuspendIstream(pool, traits.CreateInput(input_pool),
							       instance.event_loop,
							       std::chrono::milliseconds{1}));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	run_istream(traits.options, instance, std::move(pool),
		    std::move(istream), true);
}

/** normal run */
TYPED_TEST_P(IstreamFilterTest, NoBucket)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	class NoBucketIstream : public ForwardIstream {
		bool has_read = false;

	public:
		NoBucketIstream(struct pool &p, UnusedIstreamPtr _input) noexcept
			:ForwardIstream(p, std::move(_input)) {}

	protected:
		IstreamLength _GetLength() noexcept override {
			return has_read
				? ForwardIstream::_GetLength()
				: IstreamLength{.length = 0, .exhaustive = false};
		}

		void _Read() noexcept override {
			has_read = true;
			ForwardIstream::_Read();
		}

		void _FillBucketList(IstreamBucketList &list) override {
			list.SetMore();
			list.EnableFallback();
		}
	};

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 NewIstreamPtr<NoBucketIstream>(input_pool,
									traits.CreateInput(input_pool)));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024)) {}

	if (!ctx.bucket_eof && !ctx.bucket_fallback && ctx.input.IsDefined()) {
		ctx.break_ready = true;
		instance.event_loop.Run();

		if (ctx.input.IsDefined())
			while (ctx.ReadBuckets(1024 * 1024)) {}
	}

	EXPECT_TRUE(ctx.bucket_eof || ctx.bucket_fallback);
}

/** test with Istream::FillBucketList() */
TYPED_TEST_P(IstreamFilterTest, Bucket)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(ctx);
}

/** test with Istream::FillBucketList(), but don't call input.GetAvailable() */
TYPED_TEST_P(IstreamFilterTest, BucketNoGetAvailable)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	ctx.get_available_before_bucket = false;
	ctx.get_available_after_bucket = false;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(ctx);
}

/** suspend the first half of the input */
TYPED_TEST_P(IstreamFilterTest, BucketHalfSuspend)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 NewHalfSuspendIstream(pool, traits.CreateInput(input_pool),
							       instance.event_loop,
							       std::chrono::milliseconds{1}));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(ctx);
}

/** consume one more byte, expect _ConsumeBucketList() to assign this
    to the next Istream */
TYPED_TEST_P(IstreamFilterTest, BucketMore)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(1024 * 1024, true)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(ctx);
}

/** test with Istream::FillBucketList() */
TYPED_TEST_P(IstreamFilterTest, SmallBucket)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	while (ctx.ReadBuckets(3)) {}

	if (ctx.input.IsDefined())
		run_istream_ctx(ctx);
}

/** Istream::FillBucketList() throws */
TYPED_TEST_P(IstreamFilterTest, BucketError)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	const std::runtime_error error("test_fail");
	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_fail_new(pool, std::make_exception_ptr(error)));
	ASSERT_TRUE(!!istream);

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	try {
		while (ctx.ReadBuckets(3)) {}

		if (traits.options.forwards_errors) {
			FAIL();
		} else {
			ASSERT_TRUE(ctx.input.IsDefined());
			ctx.CloseInput();
		}
	} catch (...) {
		ASSERT_FALSE(ctx.input.IsDefined());
	}
}

/** Istream::FillBucketList() which throws on the second call */
TYPED_TEST_P(IstreamFilterTest, BucketSecondFail)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets ||
	    !traits.options.enable_buckets_second_fail)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	const std::runtime_error error("test_fail");
	auto istream = traits.CreateTest(instance.event_loop, pool,
					 NewSecondFailIstream(input_pool, traits.CreateInput(input_pool),
							      std::make_exception_ptr(error)));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.on_ready_buckets = true;
	ctx.get_available_before_bucket = false;
	ctx.get_available_after_bucket = false;
	ctx.fill_buckets_twice = true;

	try {
		while (ctx.ReadBucketsOrFallback(3)) {}

		if (traits.options.forwards_errors) {
			FAIL();
		} else if (ctx.input.IsDefined()) {
			ctx.CloseInput();
		}
	} catch (...) {
		ASSERT_FALSE(ctx.input.IsDefined());
	}
}

/** with ReadyIstream */
TYPED_TEST_P(IstreamFilterTest, Ready)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool, NewReadyIstream(instance.event_loop, input_pool, traits.CreateInput(input_pool)));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx{
		instance,
		std::move(pool),
		traits.options,
		std::move(istream),
	};

	ctx.on_ready_buckets = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	run_istream_ctx(ctx);
}

/** with ReadyIstream and ready_eof_ok */
TYPED_TEST_P(IstreamFilterTest, ReadyOk)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_buckets)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool, NewReadyIstream(instance.event_loop, input_pool, traits.CreateInput(input_pool)));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx{
		instance,
		std::move(pool),
		traits.options,
		std::move(istream),
	};

	ctx.on_ready_buckets = true;
	ctx.ready_eof_ok = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	run_istream_ctx(ctx);
}

/** invoke Istream::Skip(1) */
TYPED_TEST_P(IstreamFilterTest, Skip)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	ASSERT_TRUE(!!istream);
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options, std::move(istream));
	ctx.record = ctx.options.expected_result.data() != nullptr;
	ctx.Skip(1);

	run_istream_ctx(ctx);
}

/** block once after n data() invocations */
TYPED_TEST_P(IstreamFilterTest, Block)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_blocking)
		GTEST_SKIP();

	for (int n = 0; n < 8; ++n) {
		auto pool = pool_new_linear(instance.root_pool, "test", 8192);
		auto input_pool = pool_new_linear(instance.root_pool, "input",
						  8192);

		auto istream = traits.CreateTest(instance.event_loop, pool,
						 traits.CreateInput(input_pool));
		ASSERT_TRUE(!!istream);
		input_pool.reset();

		run_istream_block(traits.options, instance, std::move(pool),
				  std::move(istream), true, n);
	}
}

/** test with istream_byte */
TYPED_TEST_P(IstreamFilterTest, Byte)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_blocking || !traits.options.enable_byte)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  istream_byte_new(input_pool,
						   traits.CreateInput(input_pool)));
	input_pool.reset();

	run_istream(traits.options, instance, std::move(pool),
		    std::move(istream), true);
}

/** block and consume one byte at a time */
TYPED_TEST_P(IstreamFilterTest, BlockByte)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_blocking || !traits.options.enable_byte)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_byte_new(input_pool,
							  traits.CreateInput(input_pool)));
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.block_byte = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	run_istream_ctx(ctx);
}

/** error occurs while blocking */
TYPED_TEST_P(IstreamFilterTest, BlockInject)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_blocking)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool,
					 traits.CreateInput(input_pool));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 std::move(inject.first));

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.block_inject = std::move(inject.second);

	run_istream_ctx(ctx);

	ASSERT_TRUE(ctx.eof);
}

/** accept only half of the data */
TYPED_TEST_P(IstreamFilterTest, Half)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 traits.CreateInput(input_pool));
	input_pool.reset();

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.half = true;
	if (ctx.options.expected_result.data() != nullptr)
		ctx.record = true;

	run_istream_ctx(ctx);
}

/** input fails */
TYPED_TEST_P(IstreamFilterTest, Fail)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	const std::runtime_error error("test_fail");
	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_fail_new(pool, std::make_exception_ptr(error)));

	Context ctx{
		instance, std::move(pool),
		traits.options,
		std::move(istream),
	};

	run_istream_ctx(ctx);

	if (traits.options.forwards_errors) {
		EXPECT_TRUE(ctx.error);
	}
}

/** input fails after the first byte */
TYPED_TEST_P(IstreamFilterTest, FailAfterFirstByte)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

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

	Context ctx{
		instance, std::move(pool),
		traits.options,
		std::move(istream),
	};

	run_istream_ctx(ctx);

	if (traits.options.forwards_errors) {
		EXPECT_TRUE(ctx.error);
	}
}

TYPED_TEST_P(IstreamFilterTest, CloseInHandler)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto input = traits.CreateInput(input_pool);
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool, std::move(input));

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.close_after = 0;

	run_istream_ctx(ctx);
}

/** abort without handler */
TYPED_TEST_P(IstreamFilterTest, AbortWithoutHandler)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

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
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_abort_istream)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool, traits.CreateInput(input_pool));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool, std::move(inject.first));

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.block_after = -1;
	ctx.abort_istream = std::move(inject.second);

	ctx.WaitForEndOfStream();

	ASSERT_EQ(ctx.abort_istream, nullptr);
}

/** abort in handler, with some data consumed */
TYPED_TEST_P(IstreamFilterTest, AbortInHandlerHalf)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_abort_istream || !traits.options.enable_blocking || !traits.options.enable_byte)
		GTEST_SKIP();

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto inject = istream_inject_new(input_pool,
					 istream_four_new(input_pool,
							  traits.CreateInput(input_pool)));
	input_pool.reset();

	auto istream = traits.CreateTest(instance.event_loop, pool,
					 istream_byte_new(pool, std::move(inject.first)));

	Context ctx(instance, std::move(pool),
		    traits.options,
		    std::move(istream));
	ctx.half = true;
	ctx.abort_after = 2;
	ctx.abort_istream = std::move(inject.second);

	ctx.WaitForEndOfStream();

	ASSERT_TRUE(ctx.abort_istream == nullptr || ctx.abort_after >= 0);
}

/** abort after 1 byte of output */
TYPED_TEST_P(IstreamFilterTest, AbortAfter1Byte)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream = istream_head_new(pool,
					traits.CreateTest(instance.event_loop, pool,
							  traits.CreateInput(input_pool)),
					1, false);
	input_pool.reset();

	run_istream(traits.options, instance, std::move(pool),
		    std::move(istream), false);
}

/** test with istream_later filter */
TYPED_TEST_P(IstreamFilterTest, Later)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto input_pool = pool_new_linear(instance.root_pool, "input", 8192);

	auto istream =
		traits.CreateTest(instance.event_loop, pool,
				  istream_later_new(input_pool, traits.CreateInput(input_pool),
						    instance.event_loop));
	input_pool.reset();

	run_istream(traits.options, instance, std::move(pool),
		    std::move(istream), true);
}

/** test with large input and blocking handler */
TYPED_TEST_P(IstreamFilterTest, BigHold)
{
	auto &traits = this->traits_;
	auto &instance = this->instance_;

	if (!traits.options.enable_big || traits.options.expected_result.data() == nullptr)
		GTEST_SKIP();

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
}

REGISTER_TYPED_TEST_SUITE_P(IstreamFilterTest,
			    Normal,
			    HalfSuspend,
			    NoBucket,
			    Bucket,
			    BucketNoGetAvailable,
			    BucketHalfSuspend,
			    BucketMore,
			    SmallBucket,
			    BucketError,
			    BucketSecondFail,
			    Ready,
			    ReadyOk,
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
			    Later,
			    BigHold);
