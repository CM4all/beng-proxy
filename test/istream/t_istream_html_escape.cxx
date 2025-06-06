// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "escape/HTML.hxx"
#include "escape/Istream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamHtmlEscapeTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "test&lt;foo&amp;bar&gt;test&quot;test&apos;",
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "test<foo&bar>test\"test'");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_escape_new(pool, std::move(input), html_escape_class);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(HtmlEscape, IstreamFilterTest,
			       IstreamHtmlEscapeTestTraits);
