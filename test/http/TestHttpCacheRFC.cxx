// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/cache/RFC.hxx"
#include "http/cache/Info.hxx"
#include "http/Status.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

namespace {

struct Instance {
	RootPool root_pool;

	/**
	 * Evaluate a response with the given headers; the request is
	 * a plain GET to a local (non-remote) server, so no "Date"
	 * response header is required.
	 */
	std::optional<HttpCacheResponseInfo> Evaluate(std::initializer_list<std::pair<const char *, const char *>> h,
						      bool eager_cache=false) {
		const AllocatorPtr alloc{root_pool};

		const HttpCacheRequestInfo request_info{
			.if_match = nullptr,
			.if_none_match = nullptr,
			.if_modified_since = nullptr,
			.if_unmodified_since = nullptr,
			.is_remote = false,
			.no_cache = false,
			.only_if_cached = false,
			.has_query_string = false,
		};

		StringMap headers;
		for (const auto &i : h)
			headers.Add(alloc, i.first, i.second);

		return http_cache_response_evaluate(request_info,
						    std::chrono::system_clock::now(),
						    alloc,
						    eager_cache,
						    HttpStatus::OK, headers,
						    1024);
	}
};

} // anonymous namespace

/**
 * A response without a cookie is cacheable (control group).
 */
TEST(HttpCacheRFC, NoCookie)
{
	Instance instance;

	EXPECT_TRUE(instance.Evaluate({{"cache-control", "max-age=300"}}));
}

/**
 * A response which sets a cookie must not be stored in the shared
 * cache.
 */
TEST(HttpCacheRFC, SetCookie)
{
	Instance instance;

	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300"},
					{"set-cookie", "a=b"}}));

	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300"},
					{"set-cookie2", "a=b"}}));

	/* ... not even with a validator instead of an expiry */
	EXPECT_FALSE(instance.Evaluate({{"last-modified", "Fri, 30 Aug 2024 12:00:00 GMT"},
					{"set-cookie", "a=b"}}));
}

/**
 * ... unless the origin declares the response shareable.
 */
TEST(HttpCacheRFC, SetCookieShareable)
{
	Instance instance;

	EXPECT_TRUE(instance.Evaluate({{"cache-control", "max-age=300, public"},
				       {"set-cookie", "a=b"}}));

	EXPECT_TRUE(instance.Evaluate({{"cache-control", "s-maxage=300, max-age=300"},
				       {"set-cookie", "a=b"}}));

	/* directive names are case-insensitive */
	EXPECT_TRUE(instance.Evaluate({{"cache-control", "max-age=300, PUBLIC"},
				       {"set-cookie", "a=b"}}));
}

/**
 * "private", "no-cache" and "no-store" are case-insensitive and may
 * carry an argument.
 */
TEST(HttpCacheRFC, CaseInsensitiveDirectives)
{
	Instance instance;

	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300, Private"}}));
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300, NO-STORE"}}));
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300, No-Cache"}}));

	/* the argument form, @see RFC 9111 5.2.2.4 */
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300, no-cache=\"set-cookie\""}}));
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300, private=\"set-cookie\""}}));

	/* "max-age" is case-insensitive, too */
	EXPECT_TRUE(instance.Evaluate({{"cache-control", "MAX-AGE=300"}}));

	/* a directive which merely starts with a known name is not a
	   match */
	EXPECT_TRUE(instance.Evaluate({{"cache-control", "max-age=300, no-cache-foo"}}));
}
