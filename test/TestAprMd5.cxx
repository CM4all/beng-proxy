// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "bp/AprMd5.hxx"

#include <gtest/gtest.h>

TEST(AprMd5, Basic)
{
	EXPECT_STREQ(AprMd5("myPassword", "r31.....").c_str(),
		     "$apr1$r31.....$HqJZimcKQFAMYayBlzkrA/");
	EXPECT_STREQ(AprMd5("myPassword", "$apr1$r31.....$").c_str(),
		     "$apr1$r31.....$HqJZimcKQFAMYayBlzkrA/");
	EXPECT_STREQ(AprMd5("myPassword", "$apr1$r31.....$garbage").c_str(),
		     "$apr1$r31.....$HqJZimcKQFAMYayBlzkrA/");
	EXPECT_STREQ(AprMd5("myPassword", "qHDFfhPC").c_str(),
		     "$apr1$qHDFfhPC$nITSVHgYbDAK1Y0acGRnY0");
	EXPECT_STREQ(AprMd5("testtest", "BFRpc/..").c_str(),
		     "$apr1$BFRpc/..$va6S0zH2wTtYYzeTt147b/");
}
