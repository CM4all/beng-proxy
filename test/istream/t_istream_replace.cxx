// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/LengthIstream.hxx"
#include "istream/OptionalIstream.hxx"
#include "istream/PauseIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "util/SpanCast.hxx"
#include "BlockingIstreamHandler.hxx"

using std::string_view_literals::operator""sv;

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
		EXPECT_TRUE(list.HasMore());
	}

	EXPECT_EQ(replace->GetAvailable(false), -1);
	EXPECT_EQ(replace->GetAvailable(true), 0);

	/* add one (blocking) replacement: all data up to this
	   replacement should be available */

	auto [i1, c1] = istream_optional_new(pool, istream_string_new(pool, "123"));
	replace->Add(3, 4, std::move(i1));

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_TRUE(list.HasMore());

		auto i = list.begin();
		ASSERT_NE(i, list.end());
		EXPECT_TRUE(i->IsBuffer());
		EXPECT_EQ(ToStringView(i->GetBuffer()), "abc");

		++i;
		ASSERT_EQ(i, list.end());
	}

	EXPECT_EQ(replace->GetAvailable(false), -1);
	EXPECT_EQ(replace->GetAvailable(true), 3);

	/* unblock this replacement */

	c1->Resume();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_TRUE(list.HasMore());

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

	EXPECT_EQ(replace->GetAvailable(false), -1);
	EXPECT_EQ(replace->GetAvailable(true), 6);

	/* increase the "settled" position */

	replace->Settle(6);

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_TRUE(list.HasMore());

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

	EXPECT_EQ(replace->GetAvailable(false), -1);
	EXPECT_EQ(replace->GetAvailable(true), 8);

	/* finish */

	replace->Finish();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_TRUE(list.HasMore());

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

	EXPECT_EQ(replace->GetAvailable(false), 28);
	EXPECT_EQ(replace->GetAvailable(true), 28);

	/* unpause */

	pause_control->Resume();

	{
		IstreamBucketList list;
		replace->FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());
		EXPECT_FALSE(list.HasMore());

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

	EXPECT_EQ(replace->GetAvailable(false), 28);
	EXPECT_EQ(replace->GetAvailable(true), 28);

	/* cleanup */

	EXPECT_EQ(handler.state, BlockingIstreamHandler::State::OPEN);
	replace->Close();
}
