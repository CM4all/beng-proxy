// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

class IstreamDechunkTestTraits {
	class MyDechunkHandler final : public DechunkHandler {
		void OnDechunkEndSeen() noexcept override {}

		bool OnDechunkEnd() noexcept override {
			return false;
		}
	};

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "foo123456789",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "3\r\nfoo\r\n"
					  "1\r\n1\r\n"
					  "1\r\n2\r\n"
					  "1\r\n3\r\n"
					  "1\r\n4\r\n"
					  "1\r\n5\r\n"
					  "1\r\n6\r\n"
					  "1\r\n7\r\n"
					  "1\r\n8\r\n"
					  "1\r\n9\r\n"
					  "0\r\n\r\n ");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *handler = NewFromPool<MyDechunkHandler>(pool);
		return istream_dechunk_new(pool, std::move(input),
					   event_loop, *handler);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Dechunk, IstreamFilterTest,
			      IstreamDechunkTestTraits);
