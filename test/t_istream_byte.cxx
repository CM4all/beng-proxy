// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamByteTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_byte_new(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Byte, IstreamFilterTest,
			      IstreamByteTestTraits);
