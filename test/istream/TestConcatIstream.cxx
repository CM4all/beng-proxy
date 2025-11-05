// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "../RecordingStringSinkHandler.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/NoBucketIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream.hxx"
#include "istream/BlockSink.hxx"
#include "istream/UnusedPtr.hxx"

using std::string_view_literals::operator""sv;

class IstreamCatTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewConcatIstream(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Cat, IstreamFilterTest,
			       IstreamCatTestTraits);

/**
 * Test for a bug introduced by commit 5cb558a4cd9a9ddd8380581265
 * fixed by commit d5decf0585667354cf19d88250
 */
TEST(ConcatIstream, SecondReady)
{
	PInstance instance;
	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto [delayed, delayed_control] = istream_delayed_new(*pool, instance.event_loop);

	BlockSink sink{NewConcatIstream(*pool, istream_block_new(*pool), std::move(delayed))};

	instance.event_loop.Run();

	delayed_control.Set(istream_null_new(*pool));

	instance.event_loop.Run();

	pool.reset();
	pool_commit();
}

/**
 * First input blocks, second input requires fallback.  When the first
 * input becomes ready, fallback must be invoked on the second input.
 */
TEST(ConcatIstream, Fallback)
{
	Instance instance;

	auto &pool = instance.root_pool;

	auto [delayed, control] = istream_delayed_new(pool, instance.event_loop);
	auto concat = NewConcatIstream(pool, std::move(delayed),
				       NewIstreamPtr<NoBucketIstream>(pool, istream_string_new(pool, "x"sv)));

	RecordingStringSinkHandler handler;
	NewStringSink(pool, std::move(concat), handler, handler.cancel_ptr);

	/* unblock the first input (asynchronously) - will, be handled
	   by the EventLoop*/
	control.Set(istream_null_new(pool));
	instance.event_loop.Run();

	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "x"sv);
}
