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

#include "uri/Relative.hxx"
#include "puri_relative.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"
#include "util/StringView.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <string.h>

TEST(UriRelativeTest, Compress)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	ASSERT_STREQ(uri_compress(alloc, "/foo/bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/foo/./bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/./foo/bar"), "/foo/bar");
	ASSERT_STREQ(uri_compress(alloc, "/foo/bar/./"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "./foo/bar/"), "foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/foo//bar/"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/foo///bar/"), "/foo/bar/");
	ASSERT_STREQ(uri_compress(alloc, "/1/2/../3/"), "/1/3/");
	ASSERT_STREQ(uri_compress(alloc, "/1/2/../../3/"), "/3/");
	ASSERT_STREQ(uri_compress(alloc, "foo/../bar"), "bar");
	ASSERT_STREQ(uri_compress(alloc, "foo//../bar"), "bar");
	ASSERT_STREQ(uri_compress(alloc, "foo/.."), "");
	ASSERT_STREQ(uri_compress(alloc, "foo/../."), "");

	ASSERT_EQ(uri_compress(alloc, "/1/2/../../../3/"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/../"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/a/../../"), nullptr);
	ASSERT_EQ(uri_compress(alloc, "/.."), nullptr);
	ASSERT_EQ(uri_compress(alloc, ".."), nullptr);
	ASSERT_STREQ(uri_compress(alloc, "/1/2/.."), "/1/");
}

TEST(UriRelativeTest, Absolute)
{
	TestPool pool;
	AllocatorPtr alloc(pool);

	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/", "foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar", "foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "foo"), "http://localhost/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/", "/foo"), "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/bar/foo"),
		     "http://localhost/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/bar/",
				  "http://localhost/foo"),
		     "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "http://localhost", "foo"),
		     "http://localhost/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/", "foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar", "foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar/", "foo"), "/bar/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar/", "/foo"), "/foo");
	ASSERT_STREQ(uri_absolute(alloc, "/bar", "?foo"), "/bar?foo");

	ASSERT_STREQ(uri_absolute(alloc, "http://localhost/foo/",
				  "//example.com/bar"),
		     "http://example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "ftp://localhost/foo/",
				  "//example.com/bar"),
		     "ftp://example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "/foo/", "//example.com/bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com/foo/", "bar"),
		     "//example.com/foo/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com/foo/", "/bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com", "bar"),
		     "//example.com/bar");

	ASSERT_STREQ(uri_absolute(alloc, "//example.com", "/bar"),
		     "//example.com/bar");
}
