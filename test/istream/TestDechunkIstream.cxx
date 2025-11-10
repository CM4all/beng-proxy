// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

#include <cassert>

class MyDechunkHandler final : public DechunkHandler {
	bool end_seen = false;

	void OnDechunkEndSeen() noexcept override {
		assert(!end_seen);

		end_seen = true;
	}

	DechunkInputAction OnDechunkEnd() noexcept override {
		assert(end_seen);

		return DechunkInputAction::CLOSE;
	}
};

class IstreamDechunkTestTraits {
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

INSTANTIATE_TYPED_TEST_SUITE_P(Dechunk, IstreamFilterTest,
			       IstreamDechunkTestTraits);

/**
 * A variant with exactly the number of chunks so the EOF chunk
 * doesn't fit into the "chunks" array.
 */
class IstreamDechunk2TestTraits {
	class MyDechunkHandler final : public DechunkHandler {
		void OnDechunkEndSeen() noexcept override {}

		DechunkInputAction OnDechunkEnd() noexcept override {
			return DechunkInputAction::CLOSE;
		}
	};

public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "12345678",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "1\r\n1\r\n"
					  "1\r\n2\r\n"
					  "1\r\n3\r\n"
					  "1\r\n4\r\n"
					  "1\r\n5\r\n"
					  "1\r\n6\r\n"
					  "1\r\n7\r\n"
					  "1\r\n8\r\n"
					  "0\r\n\r\n ");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *handler = NewFromPool<MyDechunkHandler>(pool);
		return istream_dechunk_new(pool, std::move(input),
					   event_loop, *handler);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Dechunk2, IstreamFilterTest,
			       IstreamDechunk2TestTraits);
