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

#include "uri/Extract.hxx"
#include "util/StringView.hxx"

#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>

static constexpr struct UriTests {
	const char *uri;
	const char *host_and_port;
	const char *path;
	const char *query_string;
} uri_tests[] = {
	{ "http://foo/bar", "foo", "/bar", nullptr },
	{ "https://foo/bar", "foo", "/bar", nullptr },
	{ "http://foo:8080/bar", "foo:8080", "/bar", nullptr },
	{ "http://foo", "foo", nullptr, nullptr },
	{ "http://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
	{ "whatever-scheme://foo/bar?a=b", "foo", "/bar?a=b", "a=b" },
	{ "//foo/bar", "foo", "/bar", nullptr },
	{ "//foo", "foo", nullptr, nullptr },
	{ "/bar?a=b", nullptr, "/bar?a=b", "a=b" },
	{ "bar?a=b", nullptr, "bar?a=b", "a=b" },
};

TEST(UriExtractTest, HostAndPort)
{
	for (auto i : uri_tests) {
		auto result = UriHostAndPort(i.uri);
		if (i.host_and_port == nullptr) {
			ASSERT_EQ(i.host_and_port, result.data);
			ASSERT_EQ(result.size, size_t(0));
		} else {
			ASSERT_NE(result.data, nullptr);
			ASSERT_EQ(result.size, strlen(i.host_and_port));
			ASSERT_EQ(memcmp(i.host_and_port, result.data,
					 result.size), 0);
		}
	}
}

TEST(UriExtractTest, Path)
{
	for (auto i : uri_tests) {
		auto result = UriPathQueryFragment(i.uri);
		if (i.path == nullptr)
			ASSERT_EQ(i.path, result);
		else
			ASSERT_EQ(strcmp(i.path, result), 0);
	}
}

TEST(UriExtractTest, QueryString)
{
	for (auto i : uri_tests) {
		auto result = UriQuery(i.uri);
		if (i.query_string == nullptr)
			ASSERT_EQ(i.query_string, result);
		else
			ASSERT_EQ(strcmp(i.query_string, result), 0);
	}
}
