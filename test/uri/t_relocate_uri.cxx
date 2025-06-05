// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "uri/Relocate.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

struct RelocateUriTest {
	const char *uri;
	const char *internal_host;
	const char *internal_path;
	const char *external_path;
	const char *base;
	const char *expected;
};

static constexpr RelocateUriTest relocate_uri_tests[] = {
	{ "http://internal-host/int-base/c", "internal-host", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  "https://external-host:80/ext-base/c" },

	{ "//internal-host/int-base/c", "internal-host", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  "https://external-host:80/ext-base/c" },

	{ "/int-base/c", "i", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  "https://external-host:80/ext-base/c" },

	/* fail: relative URI */
	{ "c", "internal-host", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  nullptr },

	/* fail: host mismatch */
	{ "//host-mismatch/int-base/c", "internal-host", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  nullptr },

	/* fail: internal base mismatch */
	{ "http://internal-host/wrong-base/c", "internal-host", "/int-base/request",
	  "/ext-base/request", "/ext-base/",
	  nullptr },

	/* fail: external base mismatch */
	{ "http://internal-host/int-base/c", "internal-host", "/int-base/request",
	  "/wrong-base/request", "/ext-base/",
	  nullptr },
};

static void
CheckRelocateUri(AllocatorPtr alloc, const char *uri,
		 const char *internal_host, std::string_view internal_path,
		 const char *external_scheme, const char *external_host,
		 std::string_view external_path, std::string_view base,
		 const char *expected)
{
	auto *relocated = RelocateUri(alloc, uri, internal_host, internal_path,
				      external_scheme, external_host,
				      external_path, base);
	EXPECT_STREQ(expected, relocated);
}

TEST(RelocateUri, RelocateUri)
{
	RootPool pool;

	for (const auto &i : relocate_uri_tests) {
		AllocatorPtr alloc(pool);

		CheckRelocateUri(alloc, i.uri, i.internal_host, i.internal_path,
				 "https", "external-host:80",
				 i.external_path, i.base,
				 i.expected);
	}
}
