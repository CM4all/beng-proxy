// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/ConcatIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamCatTestTraits {
public:
	static constexpr const char *expected_result = "foo";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewConcatIstream(pool, std::move(input));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Cat, IstreamFilterTest,
			      IstreamCatTestTraits);
