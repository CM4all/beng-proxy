// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "../RecordingStringSinkHandler.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/NoBucketIstream.hxx"
#include "istream/OptionalIstream.hxx"
#include "istream/PauseIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "util/SpanCast.hxx"
#include "BlockingIstreamHandler.hxx"

#include <stdexcept>
#include <utility> // for std::exchange()

using std::string_view_literals::operator""sv;

/**
 * Test input is ReplaceIstream's input.
 */
class IstreamReplaceTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "abcfoodefbarghijklmnopqrstuvwxyz",
		.enable_buckets_second_fail = false,
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "abcdefghijklmnopqrstuvwxyz");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto *replace = NewIstream<ReplaceIstream>(pool, event_loop, std::move(input));
		replace->Add(3, 3, istream_string_new(pool, "foo"sv));
		replace->Add(6, 6, istream_string_new(pool, "bar"sv));
		replace->Finish();
		return UnusedIstreamPtr(replace);
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Replace, IstreamFilterTest,
			       IstreamReplaceTestTraits);

/**
 * Test input is a substitution.
 */
class IstreamReplace2TestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "abcfoofghijklmnopqrstuvwxyz",
	};

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

INSTANTIATE_TYPED_TEST_SUITE_P(Replace2, IstreamFilterTest,
			       IstreamReplace2TestTraits);

TEST(ReplaceIstream, Buckets)
{
	Instance instance;
	BlockingIstreamHandler handler;

	auto &pool = instance.root_pool;

	auto part1 = istream_string_new(pool, "abcdefghijk");
	auto part2 = istream_string_new(pool, "lmnopqrstuvwxyz");

	auto [pause, pause_control] = NewPauseIstream(pool, instance.event_loop,
						      std::move(part2));

	auto length = NewIstreamPtr<LengthIstream>(pool, std::move(pause), 15);

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   NewConcatIstream(pool, std::move(part1),
								    std::move(length)));
	replace->SetHandler(handler);

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_TRUE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);
	}

	EXPECT_FALSE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 0);

	/* add one (blocking) replacement: all data up to this
	   replacement should be available */

	auto [i1, c1] = istream_optional_new(pool, istream_string_new(pool, "123"));
	replace->Add(3, 4, std::move(i1));

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_FALSE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 3);

	/* unblock this replacement */

	c1->Resume();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "123");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_FALSE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 6);

	/* increase the "settled" position */

	replace->Settle(6);

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "123");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "ef");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_FALSE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 8);

	/* finish */

	replace->Finish();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "123");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "efghijk");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_TRUE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 28);

	/* unpause */

	pause_control->Resume();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::NO);

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "123");

		++i;
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "efghijklmnopqrstuvwxyz");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_TRUE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 28);

	/* cleanup */

	EXPECT_EQ(handler.state, BlockingIstreamHandler::State::OPEN);
	replace->Close();
}

/**
 * First substitution blocks, second substitution requires fallback.
 * When the first substitution becomes ready, fallback must be invoked
 * on the second substitution.
 */
TEST(ReplaceIstream, Fallback)
{
	Instance instance;

	auto &pool = instance.root_pool;

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop, istream_null_new(pool));

	auto [delayed, control] = istream_delayed_new(pool, instance.event_loop);
	replace->Add(0, 0, std::move(delayed));
	replace->Add(0, 0, NewIstreamPtr<NoBucketIstream>(pool, istream_string_new(pool, "x"sv)));
	replace->Finish();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_TRUE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::PUSH);
	}

	EXPECT_FALSE(replace->GetLength().exhaustive);
	EXPECT_EQ(replace->GetLength().length, 1);

	RecordingStringSinkHandler handler;

	NewStringSink(pool, UnusedIstreamPtr{replace}, handler, handler.cancel_ptr);

	/* unblock the first substitution (asynchronously) - will, be
	   handled by the EventLoop*/
	control.Set(istream_null_new(pool));
	instance.event_loop.Run();

	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "x"sv);
}

namespace {

/**
 * An #Istream which does nothing until the test makes it fail.
 */
class FailingIstream final : public Istream {
public:
	explicit FailingIstream(struct pool &p) noexcept
		:Istream(p) {}

	void Fail() noexcept {
		DestroyError(std::make_exception_ptr(std::runtime_error{"Failed"}));
	}

	/* virtual methods from class Istream */
	void _Read() noexcept override {}
};

/**
 * Emulates the nested #CssProcessor which is created and then fails
 * (by exceeding its own size limit) while the parent #XmlProcessor is
 * inside Parse().
 */
class NestedFailureReplaceIstream final : public ReplaceIstream {
	bool *const destroyed;
	bool *const destroyed_during_parse;

	bool done = false;

public:
	NestedFailureReplaceIstream(struct pool &p, EventLoop &event_loop,
				    UnusedIstreamPtr _input,
				    bool &_destroyed,
				    bool &_destroyed_during_parse) noexcept
		:ReplaceIstream(p, event_loop, std::move(_input)),
		 destroyed(&_destroyed),
		 destroyed_during_parse(&_destroyed_during_parse) {}

	~NestedFailureReplaceIstream() noexcept override {
		*destroyed = true;
	}

protected:
	/* virtual methods from class ReplaceIstream */
	void Parse(std::span<const std::byte>) override {
		if (done)
			return;

		done = true;

		auto *substitution = NewIstream<FailingIstream>(GetPool());
		Add(0, 0, UnusedIstreamPtr{substitution});

		/* copy to the stack because this object may - against
		   the documented Parse() contract - be destroyed by
		   Fail() */
		bool *const _destroyed = destroyed;
		bool *const _destroyed_during_parse = destroyed_during_parse;

		substitution->Fail();

		*_destroyed_during_parse = *_destroyed;
	}

	void ParseEnd() override {
		Finish();
	}
};

} // anonymous namespace

/**
 * A substitution which fails while the parent is inside Parse() must
 * not destroy the parent; ReplaceIstream.hxx documents that Parse()
 * "must not destroy this #ReplaceIstream instance".  The error is
 * reported to our handler after Parse() has returned.
 */
TEST(ReplaceIstream, SubstitutionErrorDuringParse)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	bool destroyed = false, destroyed_during_parse = false;

	auto *replace =
		NewIstream<NestedFailureReplaceIstream>(pool, instance.event_loop,
							istream_string_new(pool, "abc"sv),
							destroyed,
							destroyed_during_parse);

	BlockingIstreamHandler handler;
	replace->SetHandler(handler);

	replace->Read();

	/* the parent must have survived its own Parse() ... */
	EXPECT_FALSE(destroyed_during_parse);

	/* ... but the error must have been reported afterwards */
	EXPECT_TRUE(destroyed);
	EXPECT_EQ(handler.state, BlockingIstreamHandler::State::ERROR);
}
