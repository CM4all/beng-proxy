// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/ChunkedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "pool/pool.hxx"
#include "util/SpanCast.hxx"

using std::string_view_literals::operator""sv;

class IstreamChunkedTestTraits {
public:
	static constexpr const char *expected_result = nullptr;

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_chunked_new(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Chunked, IstreamFilterTest,
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
