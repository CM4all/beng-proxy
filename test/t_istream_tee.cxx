// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "PInstance.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Sink.hxx"
#include "istream/TeeIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/FailIstream.hxx"
#include "istream/istream.hxx"
#include "istream/sink_close.hxx"
#include "istream/StringSink.hxx"
#include "istream/Bucket.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"
#include "util/Exception.hxx"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include <string.h>

struct StatsIstreamSink : IstreamSink {
	size_t total_data = 0;
	bool eof = false;
	std::exception_ptr error;

	template<typename I>
	explicit StatsIstreamSink(I &&_input) noexcept
		:IstreamSink(std::forward<I>(_input)) {}

	/* only here to work around -Wdelete-non-virtual-dtor */
	virtual ~StatsIstreamSink() = default;

	using IstreamSink::CloseInput;

	void Read() noexcept {
		input.Read();
	}

	void FillBucketList(IstreamBucketList &list) {
		input.FillBucketList(list);
	}

	/* virtual methods from class IstreamHandler */

	size_t OnData(std::span<const std::byte> src) noexcept override {
		total_data += src.size();
		return src.size();
	}

	void OnEof() noexcept override {
		ClearInput();
		eof = true;
	}

	void OnError(std::exception_ptr ep) noexcept override {
		ClearInput();
		error = ep;
	}
};

struct Context : StringSinkHandler {
	EventLoop &event_loop;

	std::string value;

	bool string_sink_finished = false;

	bool break_strink_sink_finished = false;

	explicit Context(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	void WaitStringSinkFinished() noexcept {
		if (string_sink_finished)
			return;

		break_strink_sink_finished = true;
		event_loop.Run();
		break_strink_sink_finished = false;

		assert(string_sink_finished);
	}

	void OnStringSinkSuccess(std::string &&_value) noexcept final {
		assert(!string_sink_finished);
		string_sink_finished = true;

		value = std::move(_value);

		if (break_strink_sink_finished)
			event_loop.Break();
	}

	void OnStringSinkError(std::exception_ptr) noexcept final {
		assert(!string_sink_finished);
		string_sink_finished = true;

		if (break_strink_sink_finished)
			event_loop.Break();
	}
};

struct BlockContext final : Context, StatsIstreamSink {
	template<typename I>
	BlockContext(EventLoop &_event_loop, I &&_input) noexcept
		:Context(_event_loop),
		 StatsIstreamSink(std::forward<I>(_input)) {}

	/* istream handler */

	size_t OnData(std::span<const std::byte>) noexcept override {
		// block
		return 0;
	}
};

/*
 * tests
 *
 */

TEST(TeeIstream, Block1)
{
	PInstance instance;
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(instance.root_pool, "test");

	auto delayed = istream_delayed_new(*pool, instance.event_loop);
	auto tee1 = NewTeeIstream(*pool, std::move(delayed.first),
				  instance.event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);

	BlockContext ctx{instance.event_loop, std::move(tee1)};

	auto &sink = NewStringSink(*pool, std::move(tee2), ctx, cancel_ptr);
	ASSERT_TRUE(ctx.value.empty());

	/* the input (istream_delayed) blocks */
	ReadStringSink(sink);
	ASSERT_TRUE(ctx.value.empty());

	/* feed data into input */
	delayed.second.Set(istream_string_new(*pool, "foo"));
	ASSERT_TRUE(ctx.value.empty());

	/* the first output (block_istream_handler) blocks */
	ReadStringSink(sink);
	ASSERT_TRUE(ctx.value.empty());

	/* close the blocking output, this should release the "tee"
	   object and restart reading (into the second output) */
	ASSERT_TRUE(ctx.error == nullptr && !ctx.eof);
	ctx.CloseInput();
	ctx.WaitStringSinkFinished();

	ASSERT_TRUE(ctx.error == nullptr && !ctx.eof);
	ASSERT_EQ(ctx.value, "foo");

	pool.reset();
	pool_commit();
}

TEST(TeeIstream, CloseData)
{
	PInstance instance;
	Context ctx{instance.event_loop};
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(instance.root_pool, "test");
	auto tee1 = NewTeeIstream(*pool, istream_string_new(*pool, "foo"),
				  instance.event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);

	sink_close_new(*pool, std::move(tee1));

	auto &sink = NewStringSink(*pool, std::move(tee2), ctx, cancel_ptr);
	ASSERT_TRUE(ctx.value.empty());

	ReadStringSink(sink);

	/* at this point, sink_close has closed itself, and istream_tee
	   should have passed the data to the StringSink */

	ASSERT_EQ(ctx.value, "foo");

	pool_commit();
}

/**
 * Close the second output after data has been consumed only by the
 * first output.  This verifies that istream_tee's "skip" attribute is
 * obeyed properly.
 */
TEST(TeeIstream, CloseSkipped)
{
	PInstance instance;
	Context ctx{instance.event_loop};
	CancellablePointer cancel_ptr;

	auto pool = pool_new_libc(instance.root_pool, "test");
	auto tee1 = NewTeeIstream(*pool, istream_string_new(*pool, "foo"),
				  instance.event_loop, false);
	auto tee2 = AddTeeIstream(tee1, false);
	auto &sink = NewStringSink(*pool, std::move(tee1), ctx, cancel_ptr);

	sink_close_new(*pool, std::move(tee2));

	ASSERT_TRUE(ctx.value.empty());

	ReadStringSink(sink);

	ASSERT_EQ(ctx.value, "foo");

	pool_commit();
}

static void
test_error(bool close_first, bool close_second,
	   bool read_first)
{
	PInstance instance;
	auto pool = pool_new_libc(instance.root_pool, "test");
	auto tee1 =
		NewTeeIstream(*pool, istream_fail_new(*pool,
						      std::make_exception_ptr(std::runtime_error("error"))),
			      instance.event_loop,
			      false);
	auto tee2 = AddTeeIstream(tee1, false);
	pool.reset();

	auto first = !close_first
		? std::make_unique<StatsIstreamSink>(std::move(tee1))
		: nullptr;
	if (close_first)
		tee1.Clear();

	auto second = !close_second
		? std::make_unique<StatsIstreamSink>(std::move(tee2))
		: nullptr;
	if (close_second)
		tee2.Clear();

	if (read_first)
		first->Read();
	else
		second->Read();

	if (!close_first) {
		ASSERT_EQ(first->total_data, 0);
		ASSERT_FALSE(first->eof);
		ASSERT_TRUE(first->error != nullptr);
	}

	if (!close_second) {
		ASSERT_EQ(second->total_data, 0);
		ASSERT_FALSE(second->eof);
		ASSERT_TRUE(second->error != nullptr);
	}

	pool_commit();
}

TEST(TeeIstream, Error1)
{
	test_error(false, false, true);
}

TEST(TeeIstream, Error2)
{
	test_error(false, false, false);
}

TEST(TeeIstream, Error3)
{
	test_error(true, false, false);
}

TEST(TeeIstream, Error4)
{
	test_error(false, true, true);
}

static void
test_bucket_error(bool close_second_early,
		  bool close_second_late)
{
	PInstance instance;
	auto pool = pool_new_libc(instance.root_pool, "test");
	auto tee1 =
		NewTeeIstream(*pool, istream_fail_new(*pool,
						      std::make_exception_ptr(std::runtime_error("error"))),
			      instance.event_loop,
			      false);
	auto tee2 = AddTeeIstream(tee1, false);
	pool.reset();

	StatsIstreamSink first(std::move(tee1));

	auto second = !close_second_late
		? std::make_unique<StatsIstreamSink>(std::move(tee2))
		: nullptr;
	if (close_second_early) {
		if (second)
			second->CloseInput();
		else
			tee2.Clear();
	}

	IstreamBucketList list;

	try {
		first.FillBucketList(list);
		ASSERT_TRUE(false);
	} catch (...) {
		ASSERT_EQ(GetFullMessage(std::current_exception()), "error");
	}

	if (close_second_late)
		tee2.Clear();

	if (!close_second_early && !close_second_late) {
		second->Read();
		ASSERT_EQ(second->total_data, 0);
		ASSERT_FALSE(second->eof);
		ASSERT_TRUE(second->error != nullptr);
	}

	pool_commit();
}

TEST(TeeIstream, BucketError1)
{
	test_bucket_error(false, false);
}

TEST(TeeIstream, BucketError2)
{
	test_bucket_error(true, false);
}

TEST(TeeIstream, BucketError3)
{
	test_bucket_error(false, true);
}
