// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/GzipIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "thread/Pool.hxx"
#include "lib/zlib/Error.hxx"
#include "util/ScopeExit.hxx"

#include <zlib.h>

#include <stdexcept>

static std::string
GunzipString(std::string_view src)
{
	z_stream z{};
	if (int result = inflateInit2(&z, 16 + MAX_WBITS); result != Z_OK)
		throw MakeZlibError(result, "inflateInit2() failed");

	AtScopeExit(&z) { inflateEnd(&z); };

	std::array<char, 8192> dest_buffer;
	z.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(src.data()));
	z.avail_in = src.size();
	z.next_out = reinterpret_cast<Bytef *>(dest_buffer.data());
	z.avail_out = dest_buffer.size();

	if (int result = inflate(&z, Z_FINISH); result == Z_STREAM_END) {
		if (z.avail_in > 0)
			throw std::runtime_error{"Garbage after zlib output"};

		return std::string{std::string_view{dest_buffer.data(), dest_buffer.size() - z.avail_out}};
	} else if (result != Z_OK)
		throw MakeZlibError(result, "inflate() failed");
	else
		throw std::runtime_error{"Incomplete zlib output"};
}

class GzipIstreamTestTraits {
	mutable EventLoop *event_loop_ = nullptr;

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.transform_result = GunzipString,
		.enable_buckets = false,
	};

	~GzipIstreamTestTraits() noexcept {
		// invoke all pending ThreadJob::Done() calls
		if (event_loop_ != nullptr)
			event_loop_->Run();

		thread_pool_stop();
		thread_pool_join();
		thread_pool_deinit();
	}

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		event_loop_ = &event_loop;

		thread_pool_set_volatile();
		return NewGzipIstream(pool,
				      thread_pool_get_queue(event_loop),
				      std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Gzip, IstreamFilterTest,
			       GzipIstreamTestTraits);
