// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "../FlushEventLoop.hxx"
#include "../RecordingStringSinkHandler.hxx"
#include "istream/DechunkIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/ByteIstream.hxx"
#include "istream/FourIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/NoBucketIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"

#include <cassert>

using std::string_view_literals::operator""sv;

struct MyDechunkHandler final : DechunkHandler {
	const DechunkInputAction action;

	enum class State {
		INITIAL,
		END_SEEN,
		END,
	} state = State::INITIAL;

	Istream *input;

	explicit constexpr MyDechunkHandler(DechunkInputAction _action) noexcept
		:action(_action) {}

	void OnDechunkEndSeen() noexcept override {
		assert(state == State::INITIAL);
		state = State::END_SEEN;
	}

	DechunkInputAction OnDechunkEnd() noexcept override {
		assert(state == State::END_SEEN);
		state = State::END;

		if (action == DechunkInputAction::DESTROYED)
			input->Close();

		return action;
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
		auto *handler = NewFromPool<MyDechunkHandler>(pool, DechunkHandler::DechunkInputAction::CLOSE);
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

/**
 * @param delayed_input wrap the input Istream in a DelayedIstream?
 * This causes all processing to be done from inside InvokeReady() and
 * tests this code path.
 */
static void
TestAction(EventLoop &event_loop, struct pool &pool,
	   DechunkHandler::DechunkInputAction action, bool buckets, bool delayed_input=false)
{
	auto real_input = istream_string_new(pool, "3\r\nFOO\r\n0\r\n\r\nBAR"sv);
	if (!buckets)
		real_input = NewIstreamPtr<NoBucketIstream>(pool, std::move(real_input));

	UnusedIstreamPtr input;
	DelayedIstreamControl *delayed_control;
	if (delayed_input) {
		auto delayed = istream_delayed_new(pool, event_loop);
		input = std::move(delayed.first);
		delayed_control = &delayed.second;
	} else
		input = std::move(real_input);


	MyDechunkHandler dechunk_handler{action};

	switch (action) {
	case DechunkHandler::DechunkInputAction::DESTROYED:
	case DechunkHandler::DechunkInputAction::ABANDON:
		/* this kludge extracts a pointer to the input */
		dechunk_handler.input = input.Steal();
		input = UnusedIstreamPtr{dechunk_handler.input};
		break;

	case DechunkHandler::DechunkInputAction::CLOSE:
		break;
	}

	auto dechunk = istream_dechunk_new(pool, std::move(input),
					   event_loop, dechunk_handler);

	RecordingStringSinkHandler handler;
	auto &sink = NewStringSink(pool, std::move(dechunk), handler, handler.cancel_ptr);
	EXPECT_EQ(dechunk_handler.state, MyDechunkHandler::State::INITIAL);
	EXPECT_TRUE(handler.IsAlive());

	if (delayed_input) {
		delayed_control->Set(std::move(real_input));

		/* here, DelayedIstream::DeferredRead() will call
		   InvokeReady() */
		FlushPending(event_loop);
	} else {
		ReadStringSink(sink);
		EXPECT_GE(dechunk_handler.state, MyDechunkHandler::State::END_SEEN);

		if (!buckets && handler.IsAlive())
			FlushPending(event_loop);
	}

	EXPECT_EQ(dechunk_handler.state, MyDechunkHandler::State::END);
	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "FOO"sv);

	if (action == DechunkHandler::DechunkInputAction::ABANDON)
		dechunk_handler.input->Close();
}

static void
TestAction(DechunkHandler::DechunkInputAction action, bool buckets, bool delayed_input=false)
{
	PInstance instance;

	{
		const auto pool = pool_new_linear(instance.root_pool, "test", 8192);
		TestAction(instance.event_loop, pool, action, buckets, delayed_input);
		pool_trash(pool);
	}

	pool_commit();
}

/**
 * Test DechunkInputAction::ABANDON.
 */
TEST(DechunkIstream, AbandonAction)
{
	TestAction(DechunkHandler::DechunkInputAction::ABANDON, false);
}

TEST(DechunkIstream, AbandonActionBuckets)
{
	TestAction(DechunkHandler::DechunkInputAction::ABANDON, true);
}

TEST(DechunkIstream, AbandonActionBuckets2)
{
	TestAction(DechunkHandler::DechunkInputAction::ABANDON, true, true);
}

/**
 * Test DechunkInputAction::CLOSE.
 */
TEST(DechunkIstream, CloseAction)
{
	TestAction(DechunkHandler::DechunkInputAction::CLOSE, false);
}

TEST(DechunkIstream, CloseActionBuckets)
{
	TestAction(DechunkHandler::DechunkInputAction::CLOSE, true);
}

TEST(DechunkIstream, CloseActionBuckets2)
{
	TestAction(DechunkHandler::DechunkInputAction::CLOSE, true, true);
}

/**
 * Test DechunkInputAction::DESTROYED.
 */
TEST(DechunkIstream, DestroyedAction)
{
	TestAction(DechunkHandler::DechunkInputAction::DESTROYED, false);
}

TEST(DechunkIstream, DestroyedActionBuckets)
{
	TestAction(DechunkHandler::DechunkInputAction::DESTROYED, true);
}

TEST(DechunkIstream, DestroyedActionBuckets2)
{
	TestAction(DechunkHandler::DechunkInputAction::DESTROYED, true, true);
}
