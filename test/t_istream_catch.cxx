// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/CatchIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/Exception.hxx"

#include <stdio.h>

static std::exception_ptr
catch_callback(std::exception_ptr ep) noexcept
{
	fprintf(stderr, "caught: %s\n", GetFullMessage(ep).c_str());
	return {};
}

class IstreamCatchTestTraits {
public:
	/* an input string longer than the "space" buffer (128 bytes) to
	   trigger bugs due to truncated OnData() buffers */
	static constexpr const char *expected_result =
		"long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long";

	static constexpr bool call_available = false;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, expected_result);
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewCatchIstream(&pool, std::move(input),
				       BIND_FUNCTION(catch_callback));
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Catch, IstreamFilterTest,
			      IstreamCatchTestTraits);
