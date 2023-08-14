// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream_html_escape.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamHtmlEscapeTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "test&lt;foo&amp;bar&gt;test&quot;test&apos;",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "test<foo&bar>test\"test'");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_html_escape_new(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(HtmlEscape, IstreamFilterTest,
			      IstreamHtmlEscapeTestTraits);
