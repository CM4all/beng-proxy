// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "fcgi/istream_fcgi.hxx"

class IstreamFcgiTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return istream_fcgi_new(pool, std::move(input), 1);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Fcgi, IstreamFilterTest,
			       IstreamFcgiTestTraits);
