// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"

#include <stdio.h>

struct DelayedTest final : Cancellable {
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		printf("delayed_abort\n");
	}
};

class IstreamDelayedTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *test = NewFromPool<DelayedTest>(pool);

		auto delayed = istream_delayed_new(pool, event_loop);
		delayed.second.cancel_ptr = *test;
		delayed.second.Set(std::move(input));
		return std::move(delayed.first);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Delayed, IstreamFilterTest,
			       IstreamDelayedTestTraits);
