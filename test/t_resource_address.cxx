/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tconstruct.hxx"
#include "ResourceAddress.hxx"
#include "file_address.hxx"
#include "cgi/Address.hxx"
#include "TestPool.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(ResourceAddressTest, AutoBase)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	static const auto cgi0 =
		MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl", nullptr, "/");
	static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

	ASSERT_EQ(ra0.AutoBase(alloc, "/"), nullptr);
	ASSERT_EQ(ra0.AutoBase(alloc, "/foo"), nullptr);

	static const auto cgi1 =
		MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl", nullptr, "foo/bar");
	static constexpr ResourceAddress ra1(ResourceAddress::Type::CGI, cgi1);

	ASSERT_EQ(ra1.AutoBase(alloc, "/"), nullptr);
	ASSERT_EQ(ra1.AutoBase(alloc, "/foo/bar"), nullptr);

	static const auto cgi2 =
		MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl", nullptr, "/bar/baz");
	static constexpr ResourceAddress ra2(ResourceAddress::Type::CGI, cgi2);

	ASSERT_EQ(ra2.AutoBase(alloc, "/"), nullptr);
	ASSERT_EQ(ra2.AutoBase(alloc, "/foobar/baz"), nullptr);

	const char *a = ra2.AutoBase(alloc, "/foo/bar/baz");
	ASSERT_NE(a, nullptr);
	ASSERT_STREQ(a, "/foo/");
}

TEST(ResourceAddressTest, BaseNoPathInfo)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	static const auto cgi0 = MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl");
	static constexpr ResourceAddress ra0(ResourceAddress::Type::CGI, cgi0);

	auto dest = ra0.SaveBase(alloc, "");
	ASSERT_TRUE(dest.IsDefined());
	ASSERT_EQ(dest.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(dest.GetCgi().path, ra0.GetCgi().path);
	ASSERT_TRUE(dest.GetCgi().path_info == nullptr ||
		    strcmp(dest.GetCgi().path_info, "") == 0);

	dest = ra0.LoadBase(alloc, "foo/bar");
	ASSERT_TRUE(dest.IsDefined());
	ASSERT_EQ(dest.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(dest.GetCgi().path, ra0.GetCgi().path);
	ASSERT_STREQ(dest.GetCgi().path_info, "foo/bar");
}

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

	static const auto cgi3 =
		MakeCgiAddress(alloc, "/usr/lib/cgi-bin/foo.pl",
			       "/foo/bar/baz",
			       "/bar/baz");
	static constexpr ResourceAddress ra3(ResourceAddress::Type::CGI, cgi3);

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

	a = ra3.SaveBase(alloc, "bar/baz");
	ASSERT_TRUE(a.IsDefined());
	ASSERT_EQ(a.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(a.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(a.GetCgi().path_info, "/");

	b = a.LoadBase(alloc, "");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(b.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(b.GetCgi().uri, "/foo/");
	ASSERT_STREQ(b.GetCgi().path_info, "/");

	b = a.LoadBase(alloc, "xyz");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(b.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(b.GetCgi().uri, "/foo/xyz");
	ASSERT_STREQ(b.GetCgi().path_info, "/xyz");

	a = ra3.SaveBase(alloc, "baz");
	ASSERT_TRUE(a.IsDefined());
	ASSERT_EQ(a.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(a.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(a.GetCgi().uri, "/foo/bar/");
	ASSERT_STREQ(a.GetCgi().path_info, "/bar/");

	b = a.LoadBase(alloc, "bar/");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(b.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(b.GetCgi().uri, "/foo/bar/bar/");
	ASSERT_STREQ(b.GetCgi().path_info, "/bar/bar/");

	b = a.LoadBase(alloc, "bar/xyz");
	ASSERT_TRUE(b.IsDefined());
	ASSERT_EQ(b.type, ResourceAddress::Type::CGI);
	ASSERT_STREQ(b.GetCgi().path, ra3.GetCgi().path);
	ASSERT_STREQ(b.GetCgi().uri, "/foo/bar/bar/xyz");
	ASSERT_STREQ(b.GetCgi().path_info, "/bar/bar/xyz");
}
