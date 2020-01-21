/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "RedirectHttps.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <string.h>

TEST(TestRedirectHttps, Basic)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "localhost", 0, "/foo"),
		     "https://localhost/foo");

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "localhost:80", 0, "/foo"),
		     "https://localhost/foo");

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "localhost:80", 443, "/foo"),
		     "https://localhost/foo");

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "localhost:80", 444, "/foo"),
		     "https://localhost:444/foo");
}

TEST(TestRedirectHttps, IPv6)
{
	TestPool pool;
	const AllocatorPtr alloc(pool);

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "::", 0, "/foo"),
		     "https://::/foo");

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "[::]:80", 0, "/foo"),
		     "https://::/foo");

	ASSERT_STREQ(MakeHttpsRedirect(alloc, "::", 444, "/foo"),
		     "https://[::]:444/foo");
}
