// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream.hxx"
#include "istream/BlockSink.hxx"
#include "istream/UnusedPtr.hxx"

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
TEST(ConcatIstreamTest, SecondReady)
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
