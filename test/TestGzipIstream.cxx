// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/GzipIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class GzipIstreamTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewGzipIstream(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Gzip, IstreamFilterTest,
			      GzipIstreamTestTraits);
