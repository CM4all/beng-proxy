// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"

class IstreamReplaceTestTraits {
public:
	static constexpr const char *expected_result = "foo";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *replace = NewIstream<ReplaceIstream>(pool, event_loop, std::move(input));
		replace->Add(0, 0, nullptr);
		replace->Add(3, 3, nullptr);
		replace->Finish();
		return UnusedIstreamPtr(replace);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Replace, IstreamFilterTest,
			      IstreamReplaceTestTraits);

class IstreamReplace2TestTraits {
public:
	static constexpr const char *expected_result =
		"abcfoofghijklmnopqrstuvwxyz";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "foo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto istream =
			istream_string_new(pool, "abcdefghijklmnopqrstuvwxyz");
		auto *replace = NewIstream<ReplaceIstream>(pool, event_loop, std::move(istream));
		replace->Add(3, 3, std::move(input));
		replace->Extend(3, 4);
		replace->Extend(3, 5);
		replace->Finish();
		return UnusedIstreamPtr(replace);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Replace2, IstreamFilterTest,
			      IstreamReplace2TestTraits);
