// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "istream/SocketPairIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamAutoPipeTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo",
		.enable_buckets = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		input = NewSocketPairIstream(pool, event_loop, std::move(input));
		return NewAutoPipeIstream(&pool, std::move(input), nullptr);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(AutoPipe, IstreamFilterTest,
			       IstreamAutoPipeTestTraits);
