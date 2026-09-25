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
#include "istream/Sink.hxx"
#include "pool/pool.hxx"

#include <exception>

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
		ctx.ReadBucketsLoop(3);
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
		ctx.ReadBucketsLoop(3);
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

	const auto result = ctx.ReadBucketsLoop(3);
	EXPECT_EQ(result, Context::BucketResult::DEPLETED);
}

namespace {

/**
 * A sink which consumes everything it is given and never uses the
 * bucket API, so that #LengthIstream takes the OnData()/OnEof() path.
 */
class PushSink final : public IstreamSink {
public:
	std::exception_ptr error;

	std::size_t consumed = 0;

	unsigned n_eof = 0, n_error = 0;

	explicit PushSink(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)) {}

	using IstreamSink::HasInput;

	void Read() noexcept {
		input.Read();
	}

private:
	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		consumed += src.size();
		return src.size();
	}

	void OnEof() noexcept override {
		ClearInput();
		++n_eof;
	}

	void OnError(std::exception_ptr &&_error) noexcept override {
		ClearInput();
		++n_error;
		error = std::move(_error);
	}
};

} // anonymous namespace

/**
 * Control group for TooShort_Push: an input of exactly the declared
 * length reaches end-of-file.
 */
TEST(LengthIstream, Exact_Push)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	PushSink sink{NewIstreamPtr<LengthIstream>(pool,
						   istream_string_new(pool, "foo"sv),
						   3)};
	sink.Read();

	EXPECT_EQ(sink.consumed, 3u);
	EXPECT_EQ(sink.n_eof, 1u);
	EXPECT_EQ(sink.n_error, 0u);
	EXPECT_FALSE(sink.HasInput());
}

/**
 * The input ends before the declared length was reached.  It has
 * destroyed itself before invoking OnEof() (DestroyEof()), so
 * #LengthIstream must not close it again while failing.
 */
TEST(LengthIstream, TooShort_Push)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	PushSink sink{NewIstreamPtr<LengthIstream>(pool,
						   istream_string_new(pool, "foo"sv),
						   6)};
	sink.Read();

	EXPECT_EQ(sink.consumed, 3u);
	EXPECT_EQ(sink.n_eof, 0u);
	EXPECT_EQ(sink.n_error, 1u);
	EXPECT_TRUE(sink.error);
	EXPECT_FALSE(sink.HasInput());
}
