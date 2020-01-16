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

#include "bp/ForwardHeaders.hxx"
#include "TestPool.hxx"
#include "strmap.hxx"
#include "product.h"
#include "util/StringCompare.hxx"

#include <gtest/gtest.h>

#include <string.h>

using namespace BengProxy;

static const char *
strmap_to_string(const StringMap &map)
{
	static char buffer[4096];
	buffer[0] = 0;

	for (const auto &i : map) {
		strcat(buffer, i.key);
		strcat(buffer, "=");
		strcat(buffer, i.value);
		strcat(buffer, ";");
	}

	return buffer;
}

static void
check_strmap(const StringMap &map, const char *p)
{
	const char *q = strmap_to_string(map);

	ASSERT_STREQ(q, p);
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
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept=1;accept-charset=utf-8;cache-control=3;from=2;");

	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    true, true, true, true, true,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept=1;accept-charset=utf-8;cache-control=3;from=2;");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::YES);
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept=1;accept-charset=utf-8;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::MANGLE);
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept=1;accept-charset=utf-8;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::BOTH);
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept=1;accept-charset=utf-8;cache-control=3;from=2;user-agent=" PRODUCT_TOKEN ";");
}

TEST(HeaderForwardTest, HostRequestHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"host", "foo"}}};
	auto a = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 true, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;host=foo;");

	settings[HeaderGroup::FORWARD] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    true, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;x-forwarded-host=foo;");

	settings[HeaderGroup::FORWARD] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;host=foo;x-forwarded-host=foo;");
}

TEST(HeaderForwardTest, AuthRequestHeaders)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"authorization", "foo"}}};
	auto a = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::MANGLE;
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::BOTH;
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::YES;
	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;authorization=foo;");
}

TEST(HeaderForwardTest, RangeRequestHeader)
{
	HeaderForwardSettings settings = HeaderForwardSettings::AllNo();

	TestPool pool;
	const AllocatorPtr alloc(pool);
	const StringMap headers{alloc, {{"range", "1-42"}}};
	auto a = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, true,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;range=1-42;");

	a = forward_request_headers(alloc, {},
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, true,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");
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
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");

	a = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, true,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;if-match=c;if-modified-since=a;if-none-match=d;if-unmodified-since=b;");

	a = forward_request_headers(alloc, {},
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, true,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(a, "accept-charset=utf-8;");
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
	check_strmap(headers, "abc=def;accept=text/*;"
		     "content-type=image/jpeg;cookie=a=b;from=foo;"
		     "referer=http://referer.example/;"
		     "via=1.1 192.168.0.1;"
		     "x-cm4all-beng-peer-subject=CN=hans;"
		     "x-cm4all-beng-user=hans;"
		     "x-cm4all-https=tls;"
		     "x-forwarded-for=10.0.0.2;");

	/* nullptr test */
	auto a = forward_request_headers(alloc, {},
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	ASSERT_STREQ(a.Remove("user-agent"), PRODUCT_TOKEN);
	check_strmap(a, "accept-charset=utf-8;"
		     "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

	/* basic test */
	headers.Add(alloc, "user-agent", "firesomething");
	auto b = forward_request_headers(*pool, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(b, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;user-agent=firesomething;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* no accept-charset forwarded */
	headers.Add(alloc, "accept-charset", "iso-8859-1");

	auto c = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(c, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;user-agent=firesomething;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* now accept-charset is forwarded */
	auto d = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, true, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(d, "accept=text/*;accept-charset=iso-8859-1;"
		     "from=foo;user-agent=firesomething;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* with request body */
	auto e = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, true, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(e, "accept=text/*;accept-charset=utf-8;"
		     "content-type=image/jpeg;from=foo;"
		     "user-agent=firesomething;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* don't forward user-agent */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::NO;
	auto f = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(f, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* mangle user-agent */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::MANGLE;
	auto g = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	ASSERT_STREQ(g.Remove("user-agent"), PRODUCT_TOKEN);
	check_strmap(g, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
		     "x-forwarded-for=10.0.0.2, 192.168.0.3;");

	/* forward via/x-forwarded-for as-is */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::NO;
	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::YES;

	auto h = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(h, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;"
		     "via=1.1 192.168.0.1;"
		     "x-forwarded-for=10.0.0.2;");

	/* no via/x-forwarded-for */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::NO;

	auto i = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(i, "accept=text/*;accept-charset=utf-8;"
		     "from=foo;");

	/* forward cookies */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::YES;

	auto j = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(j, "accept=text/*;accept-charset=utf-8;"
		     "cookie=a=b;"
		     "from=foo;");

	/* forward 2 cookies */

	headers.Add(alloc, "cookie", "c=d");

	auto k = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(k, "accept=text/*;accept-charset=utf-8;"
		     "cookie=a=b;cookie=c=d;"
		     "from=foo;");

	/* exclude one cookie */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::BOTH;

	auto l = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 "c", nullptr, nullptr, nullptr);
	check_strmap(l, "accept=text/*;accept-charset=utf-8;"
		     "cookie=a=b;"
		     "from=foo;");

	/* forward other headers */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::NO;
	settings[HeaderGroup::OTHER] = HeaderForwardMode::YES;

	auto m = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(m, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "from=foo;");

	/* forward CORS headers */

	headers.Add(alloc, "access-control-request-method", "POST");
	headers.Add(alloc, "origin", "example.com");

	auto n = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(n, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "from=foo;");

	settings[HeaderGroup::CORS] = HeaderForwardMode::YES;

	auto o = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(o, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "access-control-request-method=POST;"
		     "from=foo;"
		     "origin=example.com;");

	/* forward secure headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::YES;

	auto p = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(p, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "access-control-request-method=POST;"
		     "from=foo;"
		     "origin=example.com;"
		     "x-cm4all-beng-user=hans;");

	/* forward ssl headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::NO;
	settings[HeaderGroup::SSL] = HeaderForwardMode::YES;

	auto q = forward_request_headers(alloc, headers,
					 "192.168.0.2", "192.168.0.3",
					 false, false, false, false, false,
					 settings,
					 nullptr, nullptr, nullptr, nullptr);
	check_strmap(q, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "access-control-request-method=POST;"
		     "from=foo;"
		     "origin=example.com;"
		     "x-cm4all-beng-peer-subject=CN=hans;"
		     "x-cm4all-https=tls;");

	/* forward referer headers */

	settings[HeaderGroup::LINK] = HeaderForwardMode::YES;

	q = forward_request_headers(alloc, headers,
				    "192.168.0.2", "192.168.0.3",
				    false, false, false, false, false,
				    settings,
				    nullptr, nullptr, nullptr, nullptr);
	check_strmap(q, "abc=def;accept=text/*;accept-charset=utf-8;"
		     "access-control-request-method=POST;"
		     "from=foo;"
		     "origin=example.com;"
		     "referer=http://referer.example/;"
		     "x-cm4all-beng-peer-subject=CN=hans;"
		     "x-cm4all-https=tls;");
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
	auto a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					  "192.168.0.2", nullptr,
					  nullptr, nullptr,
					  settings);
	check_strmap(a, "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::YES);
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::MANGLE);
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;via=1.1 192.168.0.2;");

	std::fill_n(settings.modes, size_t(HeaderGroup::MAX),
		    HeaderForwardMode::BOTH);
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "accept-ranges=10;age=1;allow=2;cache-control=4;content-disposition=12;content-encoding=6;content-language=7;content-md5=8;content-range=9;content-type=11;etag=3;expires=5;last-modified=13;retry-after=14;vary=15;");
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
	auto a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					  "192.168.0.2", nullptr,
					  nullptr, nullptr,
					  settings);
	check_strmap(a, "");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::MANGLE;
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::BOTH;
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "");

	settings[HeaderGroup::AUTH] = HeaderForwardMode::YES;
	a = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
				     "192.168.0.2", nullptr,
				     nullptr, nullptr,
				     settings);
	check_strmap(a, "authentication-info=bar;www-authenticate=foo;");
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

	auto out1 = forward_response_headers(alloc, HTTP_STATUS_OK,
					     {},
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	ASSERT_EQ(out1.Remove("server"), nullptr);
	check_strmap(out1, "");

	/* response headers: basic test */

	auto out2 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	ASSERT_EQ(out2.Get("server"), nullptr);
	check_strmap(out2, "content-type=image/jpeg;");

	/* response headers: server */

	settings[HeaderGroup::CAPABILITIES] = HeaderForwardMode::YES;

	auto out3 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out3, "content-type=image/jpeg;server=apache;");

	/* response: forward via/x-forwarded-for as-is */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::YES;

	auto out4 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out4, "content-type=image/jpeg;server=apache;"
		     "via=1.1 192.168.0.1;");

	/* response: mangle via/x-forwarded-for */

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::MANGLE;

	auto out5 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out5, "content-type=image/jpeg;server=apache;"
		     "via=1.1 192.168.0.1, 1.1 192.168.0.2;");

	settings[HeaderGroup::IDENTITY] = HeaderForwardMode::NO;

	/* response: mangle "Location" */

	headers.Add(alloc, "location", "http://localhost:8080/foo/bar");

	settings[HeaderGroup::LINK] = HeaderForwardMode::NO;

	auto out5b = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					      "192.168.0.2", nullptr,
					      RelocateCallback, (struct pool *)pool,
					      settings);
	check_strmap(out5b, "content-type=image/jpeg;"
		     "server=apache;");

	settings[HeaderGroup::LINK] = HeaderForwardMode::YES;

	out5b = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					 "192.168.0.2", nullptr,
					 RelocateCallback, (struct pool *)pool,
					 settings);
	check_strmap(out5b, "content-type=image/jpeg;"
		     "location=http://localhost:8080/foo/bar;"
		     "server=apache;");

	settings[HeaderGroup::LINK] = HeaderForwardMode::MANGLE;

	out5b = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					 "192.168.0.2", nullptr,
					 RelocateCallback, (struct pool *)pool,
					 settings);
	check_strmap(out5b, "content-type=image/jpeg;"
		     "location=http://example.com/foo/bar;"
		     "server=apache;");

	settings[HeaderGroup::LINK] = HeaderForwardMode::NO;

	/* forward cookies */

	settings[HeaderGroup::COOKIE] = HeaderForwardMode::YES;

	auto out6 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out6, "content-type=image/jpeg;server=apache;"
		     "set-cookie=a=b;");

	/* forward CORS headers */

	headers.Add(alloc, "access-control-allow-methods", "POST");

	auto out7 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out7, "content-type=image/jpeg;server=apache;"
		     "set-cookie=a=b;");

	settings[HeaderGroup::CORS] = HeaderForwardMode::YES;

	auto out8 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out8, "access-control-allow-methods=POST;"
		     "content-type=image/jpeg;server=apache;"
		     "set-cookie=a=b;");

	/* forward secure headers */

	settings[HeaderGroup::SECURE] = HeaderForwardMode::YES;

	auto out9 = forward_response_headers(alloc, HTTP_STATUS_OK, headers,
					     "192.168.0.2", nullptr,
					     nullptr, nullptr,
					     settings);
	check_strmap(out9, "access-control-allow-methods=POST;"
		     "content-type=image/jpeg;server=apache;"
		     "set-cookie=a=b;"
		     "x-cm4all-beng-user=hans;");
}
