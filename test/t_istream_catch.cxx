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
	static constexpr IstreamFilterTestOptions options{
		/* an input string longer than the "space" buffer (128
		   bytes) to trigger bugs due to truncated OnData()
		   buffers */
		.expected_result = "long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long",
		.call_available = false,
		.forwards_errors = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, options.expected_result);
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewCatchIstream(&pool, std::move(input),
				       BIND_FUNCTION(catch_callback));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Catch, IstreamFilterTest,
			       IstreamCatchTestTraits);

static std::exception_ptr
catch_callback2(std::exception_ptr ep) noexcept
{
	return ep;
}

class IstreamCatchRethrowTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		/* an input string longer than the "space" buffer (128
		   bytes) to trigger bugs due to truncated OnData()
		   buffers */
		.expected_result = "long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long long",
		.call_available = false,
		.forwards_errors = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, options.expected_result);
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewCatchIstream(&pool, std::move(input),
				       BIND_FUNCTION(catch_callback2));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(CatchRethrow, IstreamFilterTest,
			       IstreamCatchRethrowTestTraits);
