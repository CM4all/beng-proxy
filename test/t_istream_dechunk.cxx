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
	static constexpr const char *expected_result = "foo";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "3\r\nfoo\r\n0\r\n\r\n ");
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

class IstreamDechunkVerbatimTestTraits {
	class MyDechunkHandler final : public DechunkHandler {
		void OnDechunkEndSeen() noexcept override {}

		bool OnDechunkEnd() noexcept override {
			return false;
		}
	};

public:
	static constexpr const char *expected_result = "3\r\nfoo\r\n0\r\n\r\n";

	/* add space at the end so we don't run into an assertion failure
	   when istream_string reports EOF but istream_dechunk has already
	   cleared its handler */
	static constexpr const char *input_text = "3\r\nfoo\r\n0\r\n\r\n ";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, input_text);
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *handler = NewFromPool<MyDechunkHandler>(pool);
		input = istream_dechunk_new(pool, std::move(input),
					    event_loop, *handler);
		istream_dechunk_check_verbatim(input);
		return input;
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(DechunkVerbatim, IstreamFilterTest,
			      IstreamDechunkVerbatimTestTraits);

class IstreamDechunkVerbatimByteTestTraits : public IstreamDechunkVerbatimTestTraits {
public:
	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		input = IstreamDechunkVerbatimTestTraits::CreateTest(event_loop, pool,
								     std::move(input));
		input = istream_byte_new(pool, std::move(input));
		return input;
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(DechunkVerbatimByte, IstreamFilterTest,
			      IstreamDechunkVerbatimByteTestTraits);

class IstreamDechunkVerbatimFourTestTraits : public IstreamDechunkVerbatimTestTraits {
public:
	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		input = IstreamDechunkVerbatimTestTraits::CreateTest(event_loop, pool,
								     std::move(input));
		input = istream_four_new(&pool, std::move(input));
		return input;
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(DechunkVerbatimFour, IstreamFilterTest,
			      IstreamDechunkVerbatimFourTestTraits);
