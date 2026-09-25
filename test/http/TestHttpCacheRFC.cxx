// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "http/cache/RFC.hxx"
#include "http/cache/Info.hxx"
#include "http/Date.hxx"
#include "http/Status.hxx"
#include "pool/RootPool.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

#include <gtest/gtest.h>

#include <string>

using std::string_view_literals::operator""sv;

namespace {

/**
 * A fixed "current time", so that the resulting time stamps are
 * deterministic.
 */
static constexpr std::chrono::system_clock::time_point now{
	std::chrono::seconds{1700000000},
};

/**
 * The value of HttpCacheResponseInfo::expires when no expiry time is
 * known.
 */
static const auto no_expiry = std::chrono::system_clock::from_time_t(-1);

struct Instance {
	RootPool root_pool;

	/**
	 * Evaluate a response with the given headers; by default, the
	 * request is a plain GET to a local (non-remote) server, so
	 * no "Date" response header is required.
	 */
	std::optional<HttpCacheResponseInfo> Evaluate(std::initializer_list<std::pair<const char *, const char *>> h,
						      bool eager_cache=false,
						      bool is_remote=false,
						      bool has_query_string=false) {
		const AllocatorPtr alloc{root_pool};

		const HttpCacheRequestInfo request_info{
			.if_match = nullptr,
			.if_none_match = nullptr,
			.if_modified_since = nullptr,
			.if_unmodified_since = nullptr,
			.is_remote = is_remote,
			.no_cache = false,
			.only_if_cached = false,
			.has_query_string = has_query_string,
		};

		StringMap headers;
		for (const auto &i : h)
			headers.Add(alloc, i.first, i.second);

		return http_cache_response_evaluate(request_info, now,
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

/*
 * HttpCacheResponseInfo
 *
 */

/**
 * "max-age" defines the expiry time stamp relative to the current
 * time.
 */
TEST(HttpCacheRFC, ExpiresFromMaxAge)
{
	Instance instance;

	const auto info = instance.Evaluate({{"cache-control", "max-age=300"}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::seconds{300});
	EXPECT_EQ(info->last_modified, nullptr);
	EXPECT_EQ(info->etag, nullptr);
	EXPECT_EQ(info->vary, nullptr);
}

/**
 * Without "max-age", the "Expires" header defines the expiry time
 * stamp.
 */
TEST(HttpCacheRFC, ExpiresFromExpiresHeader)
{
	Instance instance;

	const auto expires = now + std::chrono::hours{1};
	const std::string expires_header = http_date_format(expires);

	const auto info = instance.Evaluate({{"expires", expires_header.c_str()}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, expires);
}

/**
 * RFC 2616 14.9.3: "If a response includes both an Expires header and
 * a max-age directive, the max-age directive overrides the Expires
 * header".
 */
TEST(HttpCacheRFC, MaxAgeOverridesExpires)
{
	Instance instance;

	const std::string expires_header =
		http_date_format(now + std::chrono::hours{1});

	const auto info = instance.Evaluate({{"cache-control", "max-age=300"},
					     {"expires", expires_header.c_str()}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::seconds{300});
}

/**
 * A response with validators but no expiry is storable without an
 * expiry time stamp; it will be revalidated before each reuse.
 */
TEST(HttpCacheRFC, Validators)
{
	Instance instance;

	const auto info = instance.Evaluate({{"last-modified", "Fri, 30 Aug 2024 12:00:00 GMT"},
					     {"etag", "\"abc\""}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, no_expiry);
	EXPECT_STREQ(info->last_modified, "Fri, 30 Aug 2024 12:00:00 GMT");
	EXPECT_STREQ(info->etag, "\"abc\"");
	EXPECT_EQ(info->vary, nullptr);
}

/**
 * Without an expiry and without a validator, the response is not
 * storable ...
 */
TEST(HttpCacheRFC, NoExpiryNoValidator)
{
	Instance instance;

	EXPECT_FALSE(instance.Evaluate({}));
}

/**
 * ... unless "eager_cache" is enabled, which invents a one hour
 * expiry.
 */
TEST(HttpCacheRFC, EagerCache)
{
	Instance instance;

	const auto info = instance.Evaluate({}, true);
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::hours{1});
}

TEST(HttpCacheRFC, Vary)
{
	Instance instance;

	{
		const auto info = instance.Evaluate({{"cache-control", "max-age=300"},
						     {"vary", "accept-encoding"}});
		ASSERT_TRUE(info);
		EXPECT_STREQ(info->vary, "accept-encoding");
	}

	/* multiple "Vary" headers are concatenated */
	{
		const auto info = instance.Evaluate({{"cache-control", "max-age=300"},
						     {"vary", "accept-encoding"},
						     {"vary", "cookie"}});
		ASSERT_TRUE(info);
		EXPECT_STREQ(info->vary, "accept-encoding, cookie");
	}

	/* an empty value is ignored */
	{
		const auto info = instance.Evaluate({{"cache-control", "max-age=300"},
						     {"vary", ""}});
		ASSERT_TRUE(info);
		EXPECT_EQ(info->vary, nullptr);
	}

	/* RFC 2616 13.6: "*" never matches, so the response is not
	   storable */
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "max-age=300"},
					{"vary", "*"}}));
}

/**
 * RFC 2616 13.9: a response to a request with a query string is only
 * storable if the server provides an explicit expiration time.
 */
TEST(HttpCacheRFC, QueryString)
{
	Instance instance;

	EXPECT_FALSE(instance.Evaluate({{"last-modified", "Fri, 30 Aug 2024 12:00:00 GMT"}},
				       false, false, true));

	const auto info = instance.Evaluate({{"cache-control", "max-age=300"}},
					    false, false, true);
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::seconds{300});
}

/**
 * A remote server must send a "Date" header, and the "Expires" value
 * is adjusted by the difference between its clock and ours.
 */
TEST(HttpCacheRFC, RemoteDateOffset)
{
	Instance instance;

	const std::string expires_header =
		http_date_format(now + std::chrono::hours{1});

	/* without a "Date" header we cannot compute the offset */
	EXPECT_FALSE(instance.Evaluate({{"expires", expires_header.c_str()}},
				       false, true, false));

	/* the server clock matches ours */
	{
		const std::string date_header = http_date_format(now);

		const auto info = instance.Evaluate({{"date", date_header.c_str()},
						     {"expires", expires_header.c_str()}},
						    false, true, false);
		ASSERT_TRUE(info);
		EXPECT_EQ(info->expires, now + std::chrono::hours{1});
	}

	/* the server clock is one minute behind ours */
	{
		const std::string date_header =
			http_date_format(now - std::chrono::minutes{1});

		const auto info = instance.Evaluate({{"date", date_header.c_str()},
						     {"expires", expires_header.c_str()}},
						    false, true, false);
		ASSERT_TRUE(info);
		EXPECT_EQ(info->expires,
			  now + std::chrono::hours{1} + std::chrono::minutes{1});
	}
}

/*
 * s-maxage
 *
 */

/**
 * RFC 9111 4.2.1: in a shared cache, "s-maxage" defines the freshness
 * lifetime and overrides "max-age".
 */
TEST(HttpCacheRFC, SMaxAgeDefinesFreshness)
{
	Instance instance;

	const auto info = instance.Evaluate({{"cache-control", "s-maxage=60, max-age=86400"}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::seconds{60});
}

/**
 * ... and it overrides the "Expires" header, too.
 */
TEST(HttpCacheRFC, SMaxAgeOverridesExpires)
{
	Instance instance;

	const std::string expires_header =
		http_date_format(now + std::chrono::hours{1});

	const auto info = instance.Evaluate({{"cache-control", "s-maxage=60"},
					     {"expires", expires_header.c_str()}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, now + std::chrono::seconds{60});
}

/**
 * "s-maxage=0" means the response is stale from the start: a shared
 * cache must revalidate it before every reuse.
 */
TEST(HttpCacheRFC, SMaxAgeZero)
{
	Instance instance;

	/* with a validator, the response is storable but has no
	   expiry time stamp */
	const auto info = instance.Evaluate({{"cache-control", "s-maxage=0, max-age=300"},
					     {"last-modified", "Fri, 30 Aug 2024 12:00:00 GMT"}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, no_expiry);

	/* without a validator there is nothing left to store */
	EXPECT_FALSE(instance.Evaluate({{"cache-control", "s-maxage=0, max-age=300"}}));
}

/**
 * "s-maxage=0" must not be mistaken for an opt-in to sharing a
 * response which sets a cookie; it expresses the opposite.
 */
TEST(HttpCacheRFC, SMaxAgeZeroSetCookie)
{
	Instance instance;

	EXPECT_FALSE(instance.Evaluate({{"cache-control", "s-maxage=0, max-age=300"},
					{"set-cookie", "a=b"}}));

	/* ... while a positive "s-maxage" does permit it */
	EXPECT_TRUE(instance.Evaluate({{"cache-control", "s-maxage=300"},
				       {"set-cookie", "a=b"}}));
}

/**
 * "max-age=0" overrides a future "Expires" header (RFC 9111 4.2.1
 * gives max-age precedence).
 */
TEST(HttpCacheRFC, MaxAgeZeroOverridesExpires)
{
	Instance instance;

	const std::string expires_header =
		http_date_format(now + std::chrono::hours{1});

	const auto info = instance.Evaluate({{"cache-control", "max-age=0"},
					     {"expires", expires_header.c_str()},
					     {"etag", "\"abc\""}});
	ASSERT_TRUE(info);
	EXPECT_EQ(info->expires, no_expiry);
}
