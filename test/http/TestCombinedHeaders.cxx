// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/CombinedHeaders.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <string_view>

using std::string_view_literals::operator""sv;

namespace {

static constexpr StringMapKey key{"x-foo"};

struct Instance {
	RootPool root_pool;

	StringMap map;

	void Add(const char *value) noexcept {
		map.Add(AllocatorPtr{root_pool}, key.string, value);
	}

	std::string_view Get() noexcept {
		return GetCombinedHeader(AllocatorPtr{root_pool}, map, key, false);
	}

	bool Equals(std::string_view expected) noexcept {
		return CombinedHeaderEquals(map, key, expected);
	}

	/**
	 * Is the combined value equal to itself?  This must hold for
	 * every input, because the HTTP cache stores GetCombinedHeader()
	 * and matches later requests with CombinedHeaderEquals().
	 */
	bool RoundTrips() noexcept {
		const auto combined = Get();
		return Equals(combined.data() != nullptr ? combined : ""sv);
	}
};

} // anonymous namespace

TEST(CombinedHeaders, Absent)
{
	Instance instance;

	/* "does not exist" is reported as a nulled std::string_view,
	   which the callers use to tell it apart from a present but
	   empty header */
	EXPECT_EQ(instance.Get().data(), nullptr);
}

/**
 * A single value is returned as-is, without copying it.
 */
TEST(CombinedHeaders, One)
{
	Instance instance;
	instance.Add("bar");

	const auto value = instance.Get();
	EXPECT_EQ(value, "bar"sv);

	/* no copy was made ... */
	EXPECT_EQ(value.data(), instance.map.Get(key));

	/* ... and the buffer is null-terminated */
	EXPECT_EQ(value.data()[value.size()], '\0');
}

TEST(CombinedHeaders, OneEmpty)
{
	Instance instance;
	instance.Add("");

	EXPECT_EQ(instance.Get().data(), nullptr);
}

TEST(CombinedHeaders, Two)
{
	Instance instance;
	instance.Add("bar");
	instance.Add("baz");

	const auto value = instance.Get();
	EXPECT_EQ(value, "bar,baz"sv);
	EXPECT_EQ(value.data()[value.size()], '\0');
}

TEST(CombinedHeaders, Three)
{
	Instance instance;
	instance.Add("a");
	instance.Add("bb");
	instance.Add("ccc");

	const auto value = instance.Get();
	EXPECT_EQ(value, "a,bb,ccc"sv);
	EXPECT_EQ(value.data()[value.size()], '\0');
}

/**
 * Empty values are skipped, and they must not leave a stray comma
 * behind.
 */
TEST(CombinedHeaders, SkipEmpty)
{
	{
		Instance instance;
		instance.Add("");
		instance.Add("bar");
		EXPECT_EQ(instance.Get(), "bar"sv);
	}

	{
		Instance instance;
		instance.Add("bar");
		instance.Add("");
		EXPECT_EQ(instance.Get(), "bar"sv);
	}

	{
		Instance instance;
		instance.Add("a");
		instance.Add("");
		instance.Add("b");
		EXPECT_EQ(instance.Get(), "a,b"sv);
	}
}

/**
 * Several header lines which are all empty: the same as one empty
 * line, i.e. a nulled std::string_view.
 */
TEST(CombinedHeaders, AllEmpty)
{
	Instance instance;
	instance.Add("");
	instance.Add("");

	EXPECT_EQ(instance.Get().data(), nullptr);
}

TEST(CombinedHeaderEquals, Absent)
{
	Instance instance;

	EXPECT_TRUE(instance.Equals(""sv));
	EXPECT_FALSE(instance.Equals("bar"sv));
	EXPECT_TRUE(instance.RoundTrips());
}

TEST(CombinedHeaderEquals, One)
{
	Instance instance;
	instance.Add("bar");

	EXPECT_TRUE(instance.Equals("bar"sv));
	EXPECT_FALSE(instance.Equals(""sv));
	EXPECT_FALSE(instance.Equals("ba"sv));
	EXPECT_FALSE(instance.Equals("barx"sv));
	EXPECT_FALSE(instance.Equals("xbar"sv));
	EXPECT_TRUE(instance.RoundTrips());
}

TEST(CombinedHeaderEquals, Two)
{
	Instance instance;
	instance.Add("a");
	instance.Add("b");

	EXPECT_TRUE(instance.Equals("a,b"sv));
	EXPECT_FALSE(instance.Equals("a"sv));
	EXPECT_FALSE(instance.Equals("b"sv));
	EXPECT_FALSE(instance.Equals("b,a"sv));
	EXPECT_FALSE(instance.Equals("a,b,c"sv));
	EXPECT_FALSE(instance.Equals("ab"sv));
	EXPECT_TRUE(instance.RoundTrips());
}

/**
 * Optional whitespace after the separator is accepted (RFC 9110 5.6.1).
 */
TEST(CombinedHeaderEquals, OptionalWhitespace)
{
	Instance instance;
	instance.Add("a");
	instance.Add("b");

	EXPECT_TRUE(instance.Equals("a, b"sv));
	EXPECT_TRUE(instance.Equals("a,   b"sv));
}

/**
 * A single value which contains a comma is not the same as two
 * values... but the combined form cannot tell them apart, which is
 * inherent in RFC 9110 5.2 and therefore accepted here.
 */
TEST(CombinedHeaderEquals, CommaInValue)
{
	Instance instance;
	instance.Add("a,b");

	EXPECT_TRUE(instance.Equals("a,b"sv));
	EXPECT_TRUE(instance.RoundTrips());
}

/**
 * Empty header lines are skipped by GetCombinedHeader(), so
 * CombinedHeaderEquals() must skip them as well.
 */
TEST(CombinedHeaderEquals, SkipEmpty)
{
	{
		Instance instance;
		instance.Add("");
		instance.Add("bar");
		EXPECT_TRUE(instance.Equals("bar"sv));
		EXPECT_TRUE(instance.RoundTrips());
	}

	{
		Instance instance;
		instance.Add("bar");
		instance.Add("");
		EXPECT_TRUE(instance.Equals("bar"sv));
		EXPECT_TRUE(instance.RoundTrips());
	}

	{
		Instance instance;
		instance.Add("a");
		instance.Add("");
		instance.Add("b");
		EXPECT_TRUE(instance.Equals("a,b"sv));
		EXPECT_TRUE(instance.RoundTrips());
	}
}

TEST(CombinedHeaderEquals, AllEmpty)
{
	Instance instance;
	instance.Add("");
	instance.Add("");

	EXPECT_TRUE(instance.Equals(""sv));
	EXPECT_TRUE(instance.RoundTrips());
}
