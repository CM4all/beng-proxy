// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/local/Address.hxx"
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
