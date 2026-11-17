// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "certdb/Wildcard.hxx"

#include <gtest/gtest.h>

#include <string_view>

using std::string_view_literals::operator""sv;

/**
 * Replace the first label with an asterisk.
 */
TEST(Wildcard, Basic)
{
	EXPECT_EQ(MakeCommonNameWildcard("www.example.com"), "*.example.com"sv);
	EXPECT_EQ(MakeCommonNameWildcard("a.b.c.example.com"), "*.b.c.example.com"sv);
	EXPECT_EQ(MakeCommonNameWildcard("xn--bcher-kva.example.com"), "*.example.com"sv);

	/* the second-level domain is a label like any other */
	EXPECT_EQ(MakeCommonNameWildcard("example.com"), "*.com"sv);

	/* no case folding */
	EXPECT_EQ(MakeCommonNameWildcard("WWW.Example.Com"), "*.Example.Com"sv);
}

/**
 * There is no label which could be replaced.
 */
TEST(Wildcard, NoDot)
{
	EXPECT_EQ(MakeCommonNameWildcard(""), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("com"), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("localhost"), ""sv);
}

/**
 * A name which is already a wildcard has no wildcard: RFC 6125 allows
 * one asterisk in the leftmost label only, and the caller has already
 * looked this name up.
 */
TEST(Wildcard, AlreadyWildcard)
{
	EXPECT_EQ(MakeCommonNameWildcard("*.example.com"), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("*.com"), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("*"), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("*."), ""sv);
}

/**
 * Names which VerifyDomainName() rejects before they can reach this
 * function from a TLS connection; the "cm4all-certdb" command line
 * has no such filter, so they must at least not produce a name which
 * no certificate can have.
 */
TEST(Wildcard, Unverified)
{
	/* an empty first label is not a label */
	EXPECT_EQ(MakeCommonNameWildcard(".example.com"), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard("."), ""sv);
	EXPECT_EQ(MakeCommonNameWildcard(".."), ""sv);

	/* an asterisk which is not the whole first label is part of
	   the label being replaced */
	EXPECT_EQ(MakeCommonNameWildcard("www*.example.com"), "*.example.com"sv);

	/* the remaining labels are passed through unmodified */
	EXPECT_EQ(MakeCommonNameWildcard("www..example.com"), "*..example.com"sv);
	EXPECT_EQ(MakeCommonNameWildcard("www.example.com."), "*.example.com."sv);
}
