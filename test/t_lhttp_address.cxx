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

#include "lhttp_address.hxx"
#include "TestPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <string.h>

static LhttpAddress
MakeLhttpAddress(const char *path, const char *host_and_port,
		 const char *uri) noexcept
{
	LhttpAddress address(path);
	address.host_and_port = host_and_port;
	address.uri = uri;
	return address;
}

TEST(LhttpAddressTest, Apply)
{
	TestPool root_pool;
	AllocatorPtr alloc(root_pool);

	const auto a = MakeLhttpAddress("/bin/lhttp", "localhost:8080",
					"/foo");

	const auto *b = a.Apply(alloc, "");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->host_and_port, a.host_and_port);
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->uri, "/foo");

	b = a.Apply(alloc, "bar");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->host_and_port, a.host_and_port);
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->uri, "/bar");

	b = a.Apply(alloc, "/");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->host_and_port, a.host_and_port);
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->uri, "/");

	b = a.Apply(alloc, "http://example.com/");
	ASSERT_EQ(b, nullptr);

	b = a.Apply(alloc, "?query");
	ASSERT_NE(b, nullptr);
	ASSERT_STREQ(b->host_and_port, a.host_and_port);
	ASSERT_STREQ(b->path, a.path);
	ASSERT_STREQ(b->uri, "/foo?query");
}
