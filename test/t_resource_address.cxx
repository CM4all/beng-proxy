/*
 * Copyright 2007-2022 CM4all GmbH
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
