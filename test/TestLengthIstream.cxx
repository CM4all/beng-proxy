// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Handler.hxx"
#include "istream/New.hxx"
#include "pool/pool.hxx"

using std::string_view_literals::operator""sv;

class IstreamLengthTestTraits {
public:
	static constexpr const char *expected_result = "foobar";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foobar");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewIstreamPtr<LengthIstream>(pool, std::move(input), 6);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Length, IstreamFilterTest,
			      IstreamLengthTestTraits);