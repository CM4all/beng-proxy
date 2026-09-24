// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/SubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/SpanCast.hxx"

#include <string>

using std::string_view_literals::operator""sv;

class IstreamSubstTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "xyz bar fo fo bar bla! fo",
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "xyz foo fo fo bar blablablablubb fo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		SubstTree tree;
		tree.Add(pool, "foo", "bar");
		tree.Add(pool, "blablablubb", "!");

		return UnusedIstreamPtr(istream_subst_new(&pool, std::move(input), std::move(tree)));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Subst, IstreamFilterTest,
			       IstreamSubstTestTraits);

namespace {

/**
 * A sink which pulls buckets manually and whose OnData() always
 * returns 0, emulating a #ReplaceIstream substitution which is not
 * active yet.
 */
class BucketSink final : public IstreamSink {
public:
	bool eof = false;
	std::exception_ptr error;

	explicit BucketSink(UnusedIstreamPtr &&_input) noexcept
		:IstreamSink(std::move(_input)) {}

	void FillBucketList(IstreamBucketList &list) {
		input.FillBucketList(list);
	}

	auto ConsumeBucketList(std::size_t nbytes) noexcept {
		return input.ConsumeBucketList(nbytes);
	}

	void Read() noexcept {
		input.Read();
	}

private:
	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte>) noexcept override {
		return 0;
	}

	void OnEof() noexcept override {
		ClearInput();
		eof = true;
	}

	void OnError(std::exception_ptr &&_error) noexcept override {
		ClearInput();
		error = std::move(_error);
	}
};

static std::string
ToString(const IstreamBucketList &list) noexcept
{
	std::string result;

	for (const auto &i : list) {
		if (!i.IsBuffer())
			break;

		result.append(ToStringView(i.GetBuffer()));
	}

	return result;
}

} // anonymous namespace

/**
 * If the input ends in the middle of a substitution key, the partial
 * match is emitted verbatim - and draining it through the bucket API
 * must report end-of-file.  It used to return eof=false forever,
 * which made #ReplaceIstream spin or read from its cleared input.
 */
TEST(SubstIstream, EofAfterPartialMatch)
{
	Instance instance;

	struct pool &pool = instance.root_pool;

	SubstTree tree;
	tree.Add(pool, "foobar", "X");

	/* the input ends inside the "foobar" key */
	BucketSink sink{istream_subst_new(&pool,
					  istream_string_new(pool, "abcfoo"sv),
					  std::move(tree))};

	/* the first bucket list ends before the 'f' which may start a
	   match */
	{
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_EQ(ToString(list), "abc"sv);
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::FALLBACK);

		const auto r = sink.ConsumeBucketList(3);
		EXPECT_EQ(r.consumed, 3u);
		EXPECT_FALSE(r.eof);
	}

	/* the rest is a partial match, so there is nothing to push */
	{
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_TRUE(list.IsEmpty());
		EXPECT_EQ(list.GetMore(), IstreamBucketList::More::FALLBACK);
	}

	/* the fallback Read() feeds "foo" to the parser and then
	   reports EOF; because our OnData() returns 0, the partial
	   match is left pending */
	sink.Read();
	ASSERT_FALSE(sink.eof);
	ASSERT_FALSE(sink.error);

	/* the pending partial match is now a plain buffer ... */
	{
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_EQ(ToString(list), "foo"sv);
	}

	/* ... and consuming it must report end-of-file */
	const auto r = sink.ConsumeBucketList(3);
	EXPECT_EQ(r.consumed, 3u);
	EXPECT_TRUE(r.eof);
}

/**
 * Like EofAfterPartialMatch, but the pending partial match is
 * consumed byte by byte; only the last byte may report end-of-file.
 */
TEST(SubstIstream, PartialMatchConsumedInSteps)
{
	Instance instance;

	struct pool &pool = instance.root_pool;

	SubstTree tree;
	tree.Add(pool, "foobar", "X");

	BucketSink sink{istream_subst_new(&pool,
					  istream_string_new(pool, "foo"sv),
					  std::move(tree))};

	/* enter the pending-mismatch state */
	{
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_TRUE(list.IsEmpty());
	}

	sink.Read();
	ASSERT_FALSE(sink.eof);
	ASSERT_FALSE(sink.error);

	for (unsigned i = 0; i < 2; ++i) {
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_FALSE(list.IsEmpty());

		const auto r = sink.ConsumeBucketList(1);
		EXPECT_EQ(r.consumed, 1u);
		EXPECT_FALSE(r.eof);
	}

	{
		IstreamBucketList list;
		sink.FillBucketList(list);
		EXPECT_EQ(ToString(list), "o"sv);
	}

	const auto r = sink.ConsumeBucketList(1);
	EXPECT_EQ(r.consumed, 1u);
	EXPECT_TRUE(r.eof);
}
