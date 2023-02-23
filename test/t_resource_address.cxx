// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "tconstruct.hxx"
#include "ResourceAddress.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "TestPool.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(ResourceAddressTest, CgiApply)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	static const auto cgi0 =
		MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl", nullptr, "/foo/");
	static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

	auto result = ra0.Apply(alloc, "");
	ASSERT_EQ(&result.GetCgi(), &ra0.GetCgi());

	result = ra0.Apply(alloc, "bar");
	ASSERT_STREQ(result.GetCgi().path_info, "/foo/bar");

	result = ra0.Apply(alloc, "/bar");
	ASSERT_STREQ(result.GetCgi().path_info, "/bar");

	/* PATH_INFO is unescaped (RFC 3875 4.1.5) */
	result = ra0.Apply(alloc, "bar%2etxt");
	ASSERT_STREQ(result.GetCgi().path_info, "/foo/bar.txt");

	result = ra0.Apply(alloc, "http://localhost/");
	ASSERT_TRUE(!result.IsDefined());
}

TEST(ResourceAddressTest, Basic)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	static const FileAddress file1("/var/www/foo/bar.html");
	static constexpr ResourceAddress ra1(file1);

	static const FileAddress file2("/var/www/foo/space .txt");
	static constexpr ResourceAddress ra2(file2);

	auto a = ra1.SaveBase(alloc, "bar.html");
	ASSERT_TRUE(a.IsDefined());
	ASSERT_EQ(a.type, ResourceAddress::Type::LOCAL);
	ASSERT_STREQ(a.GetFile().base, "/var/www/foo/");
	ASSERT_STREQ(a.GetFile().path, ".");

	auto b = a.LoadBase(alloc, "index.html");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::LOCAL);
	ASSERT_STREQ(b.GetFile().base, "/var/www/foo/");
	ASSERT_STREQ(b.GetFile().path, "index.html");

	a = ra2.SaveBase(alloc, "space%20.txt");
	ASSERT_TRUE(a.IsDefined());
	ASSERT_EQ(a.type, ResourceAddress::Type::LOCAL);
	ASSERT_STREQ(a.GetFile().base, "/var/www/foo/");
	ASSERT_STREQ(a.GetFile().path, ".");

	b = a.LoadBase(alloc, "index%2ehtml");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::LOCAL);
	ASSERT_STREQ(b.GetFile().base, "/var/www/foo/");
	ASSERT_STREQ(b.GetFile().path, "index.html");
}
