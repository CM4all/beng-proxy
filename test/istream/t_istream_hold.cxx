// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamHoldTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return UnusedIstreamPtr(istream_hold_new(pool, std::move(input)));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Hold, IstreamFilterTest,
			       IstreamHoldTestTraits);
