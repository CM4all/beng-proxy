// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/ChunkedIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "pool/pool.hxx"
#include "util/SpanCast.hxx"

using std::string_view_literals::operator""sv;

class IstreamChunkedTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_chunked_new(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Chunked, IstreamFilterTest,
			       IstreamChunkedTestTraits);

TEST(IstreamChunkedTest, Custom)
{
	struct Custom final : Istream, IstreamHandler {
		bool eof;
		std::exception_ptr error;

		explicit Custom(struct pool &p):Istream(p) {}

		/* virtual methods from class Istream */

		off_t _GetAvailable(bool) noexcept override {
			return 1;
		}

		void _Read() noexcept override {}

		/* virtual methods from class IstreamHandler */

		size_t OnData(std::span<const std::byte>) noexcept override {
			InvokeData(AsBytes(" "sv));
			return 0;
		}

		void OnEof() noexcept override {
			eof = true;
		}

		void OnError(std::exception_ptr ep) noexcept override {
			error = ep;
		}
	};

	PInstance instance;
	auto pool = pool_new_linear(instance.root_pool, "test", 8192);
	auto *ctx = NewFromPool<Custom>(pool, pool);

	auto *chunked = istream_chunked_new(pool, UnusedIstreamPtr(ctx)).Steal();
	chunked->SetHandler(*ctx);

	chunked->Read();
	chunked->Close();

	pool.reset();
	pool_commit();
}

/**
 * Generate one chunk, leave the last byte of the end marker in the
 * buffer, then enable the second chunk; this used to trigger a
 * _FillBucketList() "more" miscalculation.
 */
TEST(IstreamChunkedTest, Leave1ByteInBuffer)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto delayed = istream_delayed_new(pool, instance.event_loop);

	auto chunked = istream_chunked_new(pool,
					   NewConcatIstream(pool,
							    istream_string_new(pool, "x"),
							    std::move(delayed.first)));

	Context ctx{instance, std::move(pool), {}, std::move(chunked)};

	static constexpr std::size_t CHUNK_START_SIZE = 6;
	static constexpr std::size_t CHUNK_END_SIZE = 2;
	static constexpr std::size_t EOF_SIZE = 5;

	EXPECT_EQ(ctx.input.GetAvailable(false), -1);
	EXPECT_EQ(ctx.input.GetAvailable(true), CHUNK_START_SIZE + 1 + CHUNK_END_SIZE + EOF_SIZE);

	/* consume the first chunk, leave the "\n" in the buffer */
	ctx.ReadBuckets(CHUNK_START_SIZE + 1 + CHUNK_END_SIZE - 1);

	EXPECT_EQ(ctx.input.GetAvailable(false), -1);
	EXPECT_EQ(ctx.input.GetAvailable(true), 1 + EOF_SIZE);

	delayed.second.Set(istream_string_new(ctx.test_pool, "y"));

	EXPECT_EQ(ctx.input.GetAvailable(false), 1 + CHUNK_START_SIZE + 1 + CHUNK_END_SIZE + EOF_SIZE);
	EXPECT_EQ(ctx.input.GetAvailable(true), 1 + CHUNK_START_SIZE + 1 + CHUNK_END_SIZE + EOF_SIZE);

	ctx.ReadBuckets(1 + 1 + 7);
}
