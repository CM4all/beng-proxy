// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/istream_iconv.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamIconvTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "f\xc3\xbc\xc3\xbc",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "f\xfc\xfc");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_iconv_new(pool, std::move(input), "utf-8", "iso-8859-1");
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Iconv, IstreamFilterTest,
			      IstreamIconvTestTraits);
