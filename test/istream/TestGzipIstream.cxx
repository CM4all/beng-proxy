// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/GzipIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "lib/zlib/Error.hxx"

#include <zlib.h>

#include <stdexcept>

static std::string
GunzipString(std::string_view src)
{
	z_stream z{};
	if (int result = inflateInit2(&z, 16 + MAX_WBITS); result != Z_OK)
		throw MakeZlibError(result, "inflateInit2() failed");

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
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.transform_result = GunzipString,
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewGzipIstream(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Gzip, IstreamFilterTest,
			       GzipIstreamTestTraits);
