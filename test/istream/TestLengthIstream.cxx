// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "istream/New.hxx"
#include "istream/ZeroIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/HeadIstream.hxx"
#include "istream/BlockIstream.hxx"
#include "pool/pool.hxx"

using std::string_view_literals::operator""sv;

class IstreamLengthTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewIstreamPtr<LengthIstream>(pool, std::move(input), 6);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Length, IstreamFilterTest,
			       IstreamLengthTestTraits);

static auto
CreateZero(struct pool &pool, std::size_t size) noexcept
{
	return istream_head_new(pool, istream_four_new(&pool, istream_zero_new(pool)),
				size, false);
}

TEST(LengthIstream, TooLong_Buckets)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto istream = NewIstreamPtr<LengthIstream>(pool,
						    CreateZero(pool, 63),
						    62);

	Context ctx(instance, std::move(pool), {}, std::move(istream));

	try {
		while (ctx.ReadBuckets(3)) {}
		FAIL();
	} catch (...) {
	}
}

TEST(LengthIstream, TooShort_Buckets)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto istream = NewIstreamPtr<LengthIstream>(pool,
						    CreateZero(pool, 62),
						    63);

	Context ctx(instance, std::move(pool), {}, std::move(istream));

	try {
		while (ctx.ReadBuckets(3)) {}
		FAIL();
	} catch (...) {
	}
}

/**
 * An input that blocks after the right amount of data.
 * #LengthIstream is supposed to ignore this and report eOF.
 */
TEST(LengthIstream, Block_Buckets)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto istream = NewIstreamPtr<LengthIstream>(pool,
						    NewConcatIstream(pool,
								     CreateZero(pool, 64),
								     istream_block_new(pool)),
						    64);

	Context ctx(instance, std::move(pool), {}, std::move(istream));

	while (ctx.ReadBuckets(3)) {}
}
