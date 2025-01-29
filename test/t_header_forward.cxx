// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "bp/ForwardHeaders.hxx"
#include "TestPool.hxx"
#include "strmap.hxx"
#include "product.h"
#include "http/CommonHeaders.hxx"
#include "http/Status.hxx"
#include "util/StringCompare.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

#include <map>
#include <string>

using std::string_view_literals::operator""sv;
using namespace BengProxy;

static std::multimap<std::string, std::string, std::less<>>
strmap_to_multimap(const StringMap &map) noexcept
{
	std::multimap<std::string, std::string, std::less<>> result;

	for (const auto &i : map)
		result.emplace(i.key, i.value);

	return result;
}

static std::string
multimap_to_string(const std::multimap<std::string, std::string, std::less<>> &map) noexcept
{
	std::string result;

	for (const auto &[key, value] : map) {
		result.append(key);
		result.push_back('=');
		result.append(value);
		result.push_back(';');
	}

	return result;
}

static std::string
strmap_to_string(const StringMap &map) noexcept
{
	// convert to std::multimap to ensure the output is sorted
	return multimap_to_string(strmap_to_multimap(map));
}

static StringMap
forward_request_headers(AllocatorPtr alloc, const StringMap &src,
			const char *local_host, const char *remote_host,
			bool exclude_host,
			bool with_body, bool forward_charset,
			bool forward_encoding,
			bool forward_range,
			const HeaderForwardSettings &settings,
			const char *session_cookie,
			const RealmSession *session,
			const char *user,
			const char *host_and_port, const char *uri) noexcept
{
	return forward_request_headers(alloc, src,
				       local_host, remote_host,
				       nullptr, nullptr,
				       exclude_host, with_body,
				       forward_charset,
				       forward_encoding, forward_range,
				       settings,
				       session_cookie, session, user, nullptr,
				       host_and_port, uri);
}

static StringMap
forward_request_headers(AllocatorPtr alloc, const StringMap &src,
			const char *local_host, const char *remote_host,
			bool exclude_host,
			bool with_body, bool forward_charset,
			bool forward_encoding,
			bool forward_range,
			const HeaderForwardSettings &settings) noexcept
{
	return forward_request_headers(alloc, src,
				       local_host, remote_host,
				       exclude_host, with_body,
				       forward_charset,
				       forward_encoding, forward_range,
				       settings,
				       nullptr, nullptr, nullptr,
				       nullptr, nullptr);
}

static StringMap
forward_request_headers(AllocatorPtr alloc, const StringMap &src,
			bool exclude_host,
			bool with_body, bool forward_charset,
			bool forward_encoding,
			bool forward_range,
			const HeaderForwardSettings &settings) noexcept
{
	return forward_request_headers(alloc, src,
				       "192.168.0.2", "192.168.0.3",
				       exclude_host, with_body,
				       forward_charset,
				       forward_encoding, forward_range,
				       settings);
}

TEST(HeaderForwardTest, BasicRequestHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);

	const StringMap headers{alloc,
				{
					{"accept", "1"},
					{"from", "2"},
					{"cache-control", "3"},
				}};
	auto a = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(a), "accept=1;cache-control=3;from=2;"sv);

	a = forward_request_headers(alloc, headers,
				    true, true, true, true, true,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "accept=1;cache-control=3;from=2;"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::YES);
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "accept=1;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::MANGLE);
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "accept=1;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::BOTH);
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "accept=1;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";"sv);
}

TEST(HeaderForwardTest, HostRequestHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"host", "foo"}}};
	auto a = forward_request_headers(alloc, headers,
					 true, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "host=foo;"sv);

	settings[HeaderGroup::FORWARD] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    true, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "x-forwarded-host=foo;"sv);

	settings[HeaderGroup::FORWARD] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "host=foo;x-forwarded-host=foo;"sv);
}

TEST(HeaderForwardTest, AuthRequestHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"authorization", "foo"}}};
	auto a = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::BOTH;
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::YES;
	a = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "authorization=foo;"sv);
}

TEST(HeaderForwardTest, RangeRequestHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"range", "1-42"}}};
	auto a = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	a = forward_request_headers(alloc, headers,
				    false, false, false, false, true,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "range=1-42;"sv);

	a = forward_request_headers(alloc, {},
				    false, false, false, false, true,
				    settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);
}

TEST(HeaderForwardTest, CacheRequestHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc,
				{
					{"if-modified-since", "a"},
					{"if-unmodified-since", "b"},
					{"if-match", "c"},
					{"if-none-match", "d"},
					{"if-foo", "e"},
				}};
	auto a = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	a = forward_request_headers(alloc, headers,
				    false, false, false, false, true,
				    settings);
	EXPECT_EQ(strmap_to_string(a), "if-match=c;if-modified-since=a;if-none-match=d;if-unmodified-since=b;"sv);

	a = forward_request_headers(alloc, {},
				    false, false, false, false, true,
				    settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);
}

TEST(HeaderForwardTest, RequestHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();
	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::MANGLE;
	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::YES;
	settings[HeaderGroup::COOKIE] = HeaderForwardMode::MANGLE;

	TestPool pool;
	const AllocatorPtr alloc(pool);

	StringMap headers{alloc,
			  {{"from", "foo"},
			   {"abc", "def"},
			   {"cookie", "a=b"},
			   {"content-type", "image/jpeg"},
			   {"accept", "text/*"},
			   {"via", "1.1 192.168.0.1"},
			   {"x-forwarded-for", "10.0.0.2"},
			   {"x-cm4all-beng-user", "hans"},
			   {"x-cm4all-beng-peer-subject", "CN=hans"},
			   {"x-cm4all-https", "tls"},
			   {"referer", "http://referer.example/"},
			  }};

	/* verify strmap_to_string() */
	EXPECT_EQ(strmap_to_string(headers),
		  "abc=def;accept=text/*;"
		  "content-type=image/jpeg;cookie=a=b;from=foo;"
		  "referer=http://referer.example/;"
		  "via=1.1 192.168.0.1;"
		  "x-cm4all-beng-peer-subject=CN=hans;"
		  "x-cm4all-beng-user=hans;"
		  "x-cm4all-https=tls;"
		  "x-forwarded-for=10.0.0.2;"sv);

	/* nullptr test */
	auto a = forward_request_headers(alloc, {},
					 false, false, false, false, false,
					 settings);
	ASSERT_STREQ(a.Remove("user-agent"), PRODUCT_TOKEN);
	EXPECT_EQ(strmap_to_string(a), "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

	/* basic test */
	headers.Add(alloc, user_agent_header, "firesomething");
	auto b = forward_request_headers(*pool, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(b),
		  "accept=text/*;"
		  "from=foo;user-agent=firesomething;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* no accept-charset forwarded */
	headers.Add(alloc, "accept-charset", "iso-8859-1");

	auto c = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(c),
		  "accept=text/*;"
		  "from=foo;user-agent=firesomething;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* now accept-charset is forwarded */
	auto d = forward_request_headers(alloc, headers,
					 false, false, true, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(d),
		  "accept=text/*;accept-charset=iso-8859-1;"
		  "from=foo;user-agent=firesomething;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* with request body */
	auto e = forward_request_headers(alloc, headers,
					 false, true, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(e),
		  "accept=text/*;"
		  "content-type=image/jpeg;from=foo;"
		  "user-agent=firesomething;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* don't forward user-agent */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::NO;
	auto f = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(f),
		  "accept=text/*;"
		  "from=foo;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* mangle user-agent */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::MANGLE;
	auto g = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	ASSERT_STREQ(g.Remove("user-agent"), PRODUCT_TOKEN);
	EXPECT_EQ(strmap_to_string(g),
		  "accept=text/*;"
		  "from=foo;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		  "x-forwarded-for=10.0.0.2, 192.168.0.3;"sv);

	/* forward via/x-forwarded-for as-is */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::NO;
	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::YES;

	auto h = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(h),
		  "accept=text/*;"
		  "from=foo;"
		  "via=1.1 192.168.0.1;"
		  "x-forwarded-for=10.0.0.2;"sv);

	/* no via/x-forwarded-for */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::NO;

	auto i = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(i),
		  "accept=text/*;"
		  "from=foo;"sv);

	/* forward cookies */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::YES;

	auto j = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(j),
		  "accept=text/*;"
		  "cookie=a=b;"
		  "from=foo;"sv);

	/* forward 2 cookies */

	headers.Add(alloc, "cookie", "c=d");

	auto k = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(k),
		  "accept=text/*;"
		  "cookie=a=b;cookie=c=d;"
		  "from=foo;"sv);

	/* exclude one cookie */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::BOTH;

	auto l = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 "c", nullptr, nullptr, nullptr, nullptr);
	EXPECT_EQ(strmap_to_string(l),
		  "accept=text/*;"
		  "cookie=a=b;"
		  "from=foo;"sv);

	/* forward other headers */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::NO;
	settings[HeaderGroup::OTHER] = HeaderForwardMode::YES;

	auto m = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(m),
		  "abc=def;accept=text/*;"
		  "from=foo;"sv);

	/* forward CORS headers */

	headers.Add(alloc, "access-control-request-method", "POST");
	headers.Add(alloc, "origin", "example.com");

	auto n = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(n),
		  "abc=def;accept=text/*;"
		  "from=foo;"sv);

	settings[HeaderGroup::CORS] = HeaderForwardMode::YES;

	auto o = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(o),
		  "abc=def;accept=text/*;"
		  "access-control-request-method=POST;"
		  "from=foo;"
		  "origin=example.com;"sv);

	/* forward secure headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::YES;

	auto p = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(p),
		  "abc=def;accept=text/*;"
		  "access-control-request-method=POST;"
		  "from=foo;"
		  "origin=example.com;"
		  "x-cm4all-beng-user=hans;"sv);

	/* forward ssl headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::NO;
	settings[HeaderGroup::SSL] = HeaderForwardMode::YES;

	auto q = forward_request_headers(alloc, headers,
					 false, false, false, false, false,
					 settings);
	EXPECT_EQ(strmap_to_string(q),
		  "abc=def;accept=text/*;"
		  "access-control-request-method=POST;"
		  "from=foo;"
		  "origin=example.com;"
		  "x-cm4all-beng-peer-subject=CN=hans;"
		  "x-cm4all-https=tls;"sv);

	/* forward referer headers */

	settings[HeaderGroup::LINK] = HeaderForwardMode::YES;

	q = forward_request_headers(alloc, headers,
				    false, false, false, false, false,
				    settings);
	EXPECT_EQ(strmap_to_string(q),
		  "abc=def;accept=text/*;"
		  "access-control-request-method=POST;"
		  "from=foo;"
		  "origin=example.com;"
		  "referer=http://referer.example/;"
		  "x-cm4all-beng-peer-subject=CN=hans;"
		  "x-cm4all-https=tls;"sv);
}

static const char *
RelocateCallback(const char *uri, void *ctx) noexcept
{
	auto &pool = *(struct pool *)ctx;
	const AllocatorPtr alloc(pool);
	const char *suffix = StringAfterPrefix(uri, "http://localhost:8080/");
	if (suffix != nullptr)
		return alloc.Concat("http://example.com/", suffix);

	return uri;
}

static StringMap
forward_response_headers(AllocatorPtr alloc, HttpStatus status,
			 const StringMap &src,
			 const char *(*relocate)(const char *uri, void *ctx) noexcept,
			 void *relocate_ctx,
			 const HeaderForwardSettings &settings) noexcept
{
	return forward_response_headers(alloc, status, src,
					"192.168.0.2", nullptr,
					relocate, relocate_ctx,
					settings);
}

static StringMap
forward_response_headers(AllocatorPtr alloc, HttpStatus status,
			 const StringMap &src,
			 const HeaderForwardSettings &settings) noexcept
{
	return forward_response_headers(alloc, status, src,
					nullptr, nullptr,
					settings);
}

TEST(HeaderForwardTest, BasicResponseHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc,
				{
					{"age", "1"},
					{"allow", "2"},
					{"etag", "3"},
					{"cache-control", "4"},
					{"expires", "5"},
					{"content-encoding", "6"},
					{"content-language", "7"},
					{"content-md5", "8"},
					{"content-range", "9"},
					{"accept-ranges", "10"},
					{"content-type", "11"},
					{"content-disposition", "12"},
					{"last-modified", "13"},
					{"retry-after", "14"},
					{"vary", "15"},
				}};
	auto a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::YES);
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::MANGLE);
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;via=1.1 192.168.0.2;"sv);

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::BOTH);
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;"sv);
}

TEST(HeaderForwardTest, AuthResponseHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc,
				{
					{"www-authenticate", "foo"},
					{"authentication-info", "bar"},
				}};
	auto a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::MANGLE;
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::BOTH;
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), ""sv);

	settings[HeaderGroup::AUTH] = HeaderForwardMode::YES;
	a = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(a), "authentication-info=bar;www-authenticate=foo;"sv);
}

TEST(HeaderForwardTest, ResponseHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();
	settings[HeaderGroup::LINK] = HeaderForwardMode::YES;

	TestPool pool;
	const AllocatorPtr alloc(pool);

	StringMap headers{alloc,
			  {{"server", "apache"},
			   {"abc", "def"},
			   {"set-cookie", "a=b"},
			   {"content-type", "image/jpeg"},
			   {"via", "1.1 192.168.0.1"},
			   {"x-cm4all-beng-user", "hans"},
			   {"x-cm4all-https", "tls"},
			  }};

	/* response headers: nullptr */

	auto out1 = forward_response_headers(alloc, HttpStatus::OK,
					     {},
					     settings);
	ASSERT_EQ(out1.Remove("server"), nullptr);
	EXPECT_EQ(strmap_to_string(out1), ""sv);

	/* response headers: basic test */

	auto out2 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	ASSERT_EQ(out2.Get("server"), nullptr);
	EXPECT_EQ(strmap_to_string(out2), "content-type=image/jpeg;"sv);

	/* response headers: server */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::YES;

	auto out3 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out3), "content-type=image/jpeg;server=apache;"sv);

	/* response: forward via/x-forwarded-for as-is */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::YES;

	auto out4 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out4),
		  "content-type=image/jpeg;server=apache;"
		  "via=1.1 192.168.0.1;"sv);

	/* response: mangle via/x-forwarded-for */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::MANGLE;

	auto out5 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out5),
		  "content-type=image/jpeg;server=apache;"
		  "via=1.1 192.168.0.1, 1.1 192.168.0.2;"sv);

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::NO;

	/* response: mangle "Location" */

	headers.Add(alloc, "location", "http://localhost:8080/foo/bar");

	settings[HeaderGroup::LINK] = HeaderForwardMode::NO;

	auto out5b = forward_response_headers(alloc, HttpStatus::OK, headers,
					      RelocateCallback, (struct pool *)pool,
					      settings);
	EXPECT_EQ(strmap_to_string(out5b),
		  "content-type=image/jpeg;"
		  "server=apache;"sv);

	settings[HeaderGroup::LINK] = HeaderForwardMode::YES;

	out5b = forward_response_headers(alloc, HttpStatus::OK, headers,
					 RelocateCallback, (struct pool *)pool,
					 settings);
	EXPECT_EQ(strmap_to_string(out5b),
		  "content-type=image/jpeg;"
		  "location=http://localhost:8080/foo/bar;"
		  "server=apache;"sv);

	settings[HeaderGroup::LINK] = HeaderForwardMode::MANGLE;

	out5b = forward_response_headers(alloc, HttpStatus::OK, headers,
					 RelocateCallback, (struct pool *)pool,
					 settings);
	EXPECT_EQ(strmap_to_string(out5b),
		  "content-type=image/jpeg;"
		  "location=http://example.com/foo/bar;"
		  "server=apache;"sv);

	settings[HeaderGroup::LINK] = HeaderForwardMode::NO;

	/* forward cookies */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::YES;

	auto out6 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out6),
		  "content-type=image/jpeg;server=apache;"
		  "set-cookie=a=b;"sv);

	/* forward CORS headers */

	headers.Add(alloc, "access-control-allow-methods", "POST");

	auto out7 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out7),
		  "content-type=image/jpeg;server=apache;"
		  "set-cookie=a=b;"sv);

	settings[HeaderGroup::CORS] = HeaderForwardMode::YES;

	auto out8 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out8),
		  "access-control-allow-methods=POST;"
		  "content-type=image/jpeg;server=apache;"
		  "set-cookie=a=b;"sv);

	/* forward secure headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::YES;

	auto out9 = forward_response_headers(alloc, HttpStatus::OK, headers, settings);
	EXPECT_EQ(strmap_to_string(out9),
		  "access-control-allow-methods=POST;"
		  "content-type=image/jpeg;server=apache;"
		  "set-cookie=a=b;"
		  "x-cm4all-beng-user=hans;"sv);
}
