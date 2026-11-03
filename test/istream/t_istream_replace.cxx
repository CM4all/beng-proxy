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

/**
 * Lots of adjacent empty substitutions must be drained iteratively;
 * one stack frame per substitution overflows the stack, because the
 * input decides how many of them there are.
 */
TEST(ReplaceIstream, ManyEmptySubstitutions)
{
	/* with one stack frame per substitution, this needs more
	   than 60 MB of stack */
	static constexpr std::size_t N = 50000;

	Instance instance;
	BlockingIstreamHandler handler;

	auto &pool = instance.root_pool;

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   istream_string_new(pool, "abc"sv));
	replace->SetHandler(handler);

	for (std::size_t i = 0; i < N; ++i)
		replace->Add(0, 0, istream_null_new(pool));

	replace->Finish();

	IstreamBucketList list;
	replace->FillBucketList(list);

	/* all substitutions were drained, and what follows them is
	   the buffer */

	EXPECT_EQ(list.GetMore(), IstreamBucketList::More::NO);

	auto i = list.begin();
	ASSERT_NE(i, list.end());
	EXPECT_TRUE(i->IsBuffer());
	EXPECT_EQ(ToStringView(i->GetBuffer()), "abc"sv);

	++i;
	ASSERT_EQ(i, list.end());

	/* cleanup */

	EXPECT_EQ(handler.state, BlockingIstreamHandler::State::OPEN);
	replace->Close();
}

/**
 * Adjacent deletions are coalesced into one substitution, so that many
 * deleted elements do not allocate one #Substitution each.
 */
TEST(ReplaceIstream, AdjacentDeletions)
{
	static constexpr std::size_t N = 1000;

	const std::string source = "a" + std::string(N, 'x') + "bcd";

	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   istream_string_new(pool, source));

	const std::size_t netto_size = pool_netto_size(pool);

	/* delete all the 'x' characters, one call per character;
	   these deletions are all adjacent */
	for (std::size_t i = 1; i <= N; ++i)
		replace->Add(i, i + 1, nullptr);

	/* ... which must have allocated only one #Substitution */
	EXPECT_LT(pool_netto_size(pool) - netto_size, 1024U);

	/* one more deletion ('c') which is not adjacent to the
	   previous one, because the 'b' is between them */
	replace->Add(N + 2, N + 3, nullptr);

	replace->Finish();

	RecordingStringSinkHandler handler;

	NewStringSink(pool, UnusedIstreamPtr{replace}, handler, handler.cancel_ptr);
	instance.event_loop.Run();

	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "abd"sv);
}

/**
 * A deletion which is adjacent to a substitution that inserts an
 * #Istream is merged into that substitution.
 */
TEST(ReplaceIstream, DeletionAfterSubstitution)
{
	Instance instance;

	auto pool = pool_new_linear(instance.root_pool, "test", 8192);

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   istream_string_new(pool, "abcdefghij"sv));

	/* replace the 'd' with "123" ... */
	replace->Add(3, 4, istream_string_new(pool, "123"sv));

	const std::size_t netto_size = pool_netto_size(pool);

	/* ... and delete the 'e' and the 'f', which are adjacent to
	   it */
	replace->Add(4, 5, nullptr);
	replace->Add(5, 6, nullptr);

	/* no additional #Substitution was allocated */
	EXPECT_EQ(pool_netto_size(pool), netto_size);

	replace->Finish();

	RecordingStringSinkHandler handler;

	NewStringSink(pool, UnusedIstreamPtr{replace}, handler, handler.cancel_ptr);
	instance.event_loop.Run();

	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "abc123ghij"sv);
}

/**
 * A deletion at the end of the source delivers no data, and therefore
 * _FillBucketList() reports end-of-file; _ConsumeBucketList() must
 * report the same, or else it claims that there is more data which it
 * can never deliver.
 */
TEST(ReplaceIstream, TrailingDeletion)
{
	Instance instance;

	auto &pool = instance.root_pool;

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   istream_string_new(pool, "abcdefghij"sv));

	replace->Add(9, 10, nullptr);
	replace->Finish();

	RecordingStringSinkHandler handler;

	NewStringSink(pool, UnusedIstreamPtr{replace}, handler, handler.cancel_ptr);
	instance.event_loop.Run();

	ASSERT_FALSE(handler.IsAlive());
	EXPECT_EQ(std::move(handler).TakeValue(), "abcdefghi"sv);
}

namespace {

/**
 * An #Istream whose _FillBucketList() throws (after destroying
 * itself, as the API requires).
 */
class ThrowingBucketIstream final : public Istream {
public:
	explicit ThrowingBucketIstream(struct pool &p) noexcept
		:Istream(p) {}

	void _Read() noexcept override {}

	void _FillBucketList(IstreamBucketList &) override {
		Destroy();
		throw std::runtime_error{"Failed"};
	}
};

} // anonymous namespace

/**
 * An error which is raised after an empty substitution was drained
 * must destroy this #ReplaceIstream exactly once.
 */
TEST(ReplaceIstream, ErrorAfterEmptySubstitution)
{
	Instance instance;
	auto &pool = instance.root_pool;

	auto *replace = NewIstream<ReplaceIstream>(pool, instance.event_loop,
						   istream_string_new(pool, "abc"sv));
	/* an empty substitution, which gets drained first ... */
	replace->Add(0, 0, istream_null_new(pool));
	/* ... and then one which throws */
	replace->Add(0, 0, UnusedIstreamPtr{NewIstream<ThrowingBucketIstream>(pool)});
	replace->Finish();

	BlockingIstreamHandler handler;
	replace->SetHandler(handler);

	IstreamBucketList list;
	EXPECT_THROW(replace->FillBucketList(list), std::runtime_error);
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
