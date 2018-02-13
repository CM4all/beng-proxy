/*
 * Copyright 2007-2018 Content Management AG
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

TEST(HeaderForwardTest, RequestHeaders)
{
    struct header_forward_settings settings;
    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE;
    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES;
    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SSL] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_NO;

    TestPool pool;

    StringMap headers(pool);
    headers.Add("from", "foo");
    headers.Add("abc", "def");
    headers.Add("cookie", "a=b");
    headers.Add("content-type", "image/jpeg");
    headers.Add("accept", "text/*");
    headers.Add("via", "1.1 192.168.0.1");
    headers.Add("x-forwarded-for", "10.0.0.2");
    headers.Add("x-cm4all-beng-user", "hans");
    headers.Add("x-cm4all-beng-peer-subject", "CN=hans");
    headers.Add("x-cm4all-https", "tls");
    headers.Add("referer", "http://referer.example/");

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
    auto a = forward_request_headers(pool, StringMap(pool),
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    ASSERT_STREQ(a.Remove("user-agent"), PRODUCT_TOKEN);
    check_strmap(a, "accept-charset=utf-8;"
                 "via=1.1 192.168.0.2;x-forwarded-for=192.168.0.3;");

    /* basic test */
    headers.Add("user-agent", "firesomething");
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
    headers.Add("accept-charset", "iso-8859-1");

    auto c = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(c, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* now accept-charset is forwarded */
    auto d = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, true, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(d, "accept=text/*;accept-charset=iso-8859-1;"
                 "from=foo;user-agent=firesomething;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* with request body */
    auto e = forward_request_headers(pool, headers,
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

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    auto f = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(f, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;"
                 "x-forwarded-for=10.0.0.2, 192.168.0.3;");

    /* mangle user-agent */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_MANGLE;
    auto g = forward_request_headers(pool, headers,
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

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_YES;

    auto h = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(h, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;"
                 "via=1.1 192.168.0.1;"
                 "x-forwarded-for=10.0.0.2;");

    /* no via/x-forwarded-for */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;

    auto i = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(i, "accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    auto j = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(j, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;"
                 "from=foo;");

    /* forward 2 cookies */

    headers.Add("cookie", "c=d");

    auto k = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(k, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;cookie=c=d;"
                 "from=foo;");

    /* exclude one cookie */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_BOTH;

    auto l = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     "c", nullptr, nullptr, nullptr);
    check_strmap(l, "accept=text/*;accept-charset=utf-8;"
                 "cookie=a=b;"
                 "from=foo;");

    /* forward other headers */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_YES;

    auto m = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(m, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    /* forward CORS headers */

    headers.Add("access-control-request-method", "POST");
    headers.Add("origin", "example.com");

    auto n = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                  false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(n, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "from=foo;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    auto o = forward_request_headers(pool, headers,
                                     "192.168.0.2", "192.168.0.3",
                                     false, false, false, false, false,
                                     settings,
                                     nullptr, nullptr, nullptr, nullptr);
    check_strmap(o, "abc=def;accept=text/*;accept-charset=utf-8;"
                 "access-control-request-method=POST;"
                 "from=foo;"
                 "origin=example.com;");

    /* forward secure headers */

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_YES;

    auto p = forward_request_headers(pool, headers,
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

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SSL] = HEADER_FORWARD_YES;

    auto q = forward_request_headers(pool, headers,
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

    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_YES;

    q = forward_request_headers(pool, headers,
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
    const char *suffix = StringAfterPrefix(uri, "http://localhost:8080/");
    if (suffix != nullptr)
        return p_strcat(&pool, "http://example.com/", suffix, nullptr);

    return uri;
}

TEST(HeaderForwardTest, ResponseHeaders)
{
    struct header_forward_settings settings;
    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_OTHER] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_SSL] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_NO;
    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_YES;

    TestPool pool;

    StringMap headers(pool);
    headers.Add("server", "apache");
    headers.Add("abc", "def");
    headers.Add("set-cookie", "a=b");
    headers.Add("content-type", "image/jpeg");
    headers.Add("via", "1.1 192.168.0.1");
    headers.Add("x-cm4all-beng-user", "hans");
    headers.Add("x-cm4all-https", "tls");

    /* response headers: nullptr */

    auto out1 = forward_response_headers(*pool, HTTP_STATUS_OK,
                                         StringMap(*pool),
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    ASSERT_EQ(out1.Remove("server"), nullptr);
    check_strmap(out1, "");

    /* response headers: basic test */

    auto out2 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    ASSERT_EQ(out2.Get("server"), nullptr);
    check_strmap(out2, "content-type=image/jpeg;");

    /* response headers: server */

    settings.modes[HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES;

    auto out3 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out3, "content-type=image/jpeg;server=apache;");

    /* response: forward via/x-forwarded-for as-is */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_YES;

    auto out4 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out4, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1;");

    /* response: mangle via/x-forwarded-for */

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE;

    auto out5 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out5, "content-type=image/jpeg;server=apache;"
                 "via=1.1 192.168.0.1, 1.1 192.168.0.2;");

    settings.modes[HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO;

    /* response: mangle "Location" */

    headers.Add("location", "http://localhost:8080/foo/bar");

    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_NO;

    auto out5b = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                          "192.168.0.2", nullptr,
                                          RelocateCallback, (struct pool *)pool,
                                          settings);
    check_strmap(out5b, "content-type=image/jpeg;"
                 "server=apache;");

    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_YES;

    out5b = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                     "192.168.0.2", nullptr,
                                     RelocateCallback, (struct pool *)pool,
                                     settings);
    check_strmap(out5b, "content-type=image/jpeg;"
                 "location=http://localhost:8080/foo/bar;"
                 "server=apache;");

    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_MANGLE;

    out5b = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                     "192.168.0.2", nullptr,
                                     RelocateCallback, (struct pool *)pool,
                                     settings);
    check_strmap(out5b, "content-type=image/jpeg;"
                 "location=http://example.com/foo/bar;"
                 "server=apache;");

    settings.modes[HEADER_GROUP_LINK] = HEADER_FORWARD_NO;

    /* forward cookies */

    settings.modes[HEADER_GROUP_COOKIE] = HEADER_FORWARD_YES;

    auto out6 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out6, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward CORS headers */

    headers.Add("access-control-allow-methods", "POST");

    auto out7 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out7, "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    settings.modes[HEADER_GROUP_CORS] = HEADER_FORWARD_YES;

    auto out8 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out8, "access-control-allow-methods=POST;"
                 "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;");

    /* forward secure headers */

    settings.modes[HEADER_GROUP_SECURE] = HEADER_FORWARD_YES;

    auto out9 = forward_response_headers(*pool, HTTP_STATUS_OK, headers,
                                         "192.168.0.2", nullptr,
                                         nullptr, nullptr,
                                         settings);
    check_strmap(out9, "access-control-allow-methods=POST;"
                 "content-type=image/jpeg;server=apache;"
                 "set-cookie=a=b;"
                 "x-cm4all-beng-user=hans;");
}
