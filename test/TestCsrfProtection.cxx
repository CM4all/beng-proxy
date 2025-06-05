// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "bp/CsrfToken.hxx"
#include "bp/session/Id.hxx"
#include "bp/session/Prng.hxx"

#include <gtest/gtest.h>

TEST(CsrfProtectionTest, Time)
{
	const auto now = std::chrono::system_clock::now();
	const auto a = CsrfHash::ImportTime(now);

	EXPECT_EQ(a, CsrfHash::ImportTime(CsrfHash::ExportTime(a)));
}

TEST(CsrfProtectionTest, FormatAndParse)
{
	SessionPrng prng;

	SessionId salt;
	salt.Generate(prng);
	EXPECT_TRUE(salt.IsDefined());

	CsrfToken a;
	a.Generate(std::chrono::system_clock::now(), salt);

	char s[CsrfToken::STRING_LENGTH + 1];
	a.Format(s);

	CsrfToken b;
	ASSERT_TRUE(b.Parse(s));
	EXPECT_EQ(CsrfHash::ImportTime(b.time), CsrfHash::ImportTime(a.time));
	EXPECT_EQ(b.hash, a.hash);

	char t[CsrfToken::STRING_LENGTH + 1];
	b.Format(t);

	EXPECT_STREQ(s, t);
}
