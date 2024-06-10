// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/BrotliEncoderIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

#include <brotli/decode.h>

#include <array>

static std::string
BrotliDecompressString(std::string_view src)
{
	std::array<char, 8192> decoded_buffer;

	std::size_t decoded_size = decoded_buffer.size();
	switch (BrotliDecoderDecompress(src.size(), reinterpret_cast<const uint8_t *>(src.data()),
					&decoded_size, reinterpret_cast<uint8_t *>(decoded_buffer.data()))) {
	case BROTLI_DECODER_RESULT_ERROR:
		break;

	case BROTLI_DECODER_RESULT_SUCCESS:
		return std::string{decoded_buffer.data(), decoded_size};

	case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
		throw std::runtime_error{"BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT"};

	case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
		throw std::runtime_error{"BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT"};
	}

	throw std::runtime_error{"BrotliDecoderDecompress() failed"};
}

class BrotliEncoderIstreamTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foobar",
		.transform_result = BrotliDecompressString,
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewBrotliEncoderIstream(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(BrotliEncoder, IstreamFilterTest,
			       BrotliEncoderIstreamTestTraits);
