/*
 * Copyright 2007-2017 Content Management AG
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

#include "tconstruct.hxx"
#include "http_cache.hxx"
#include "ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "http_address.hxx"
#include "GrowingBuffer.hxx"
#include "header_parser.hxx"
#include "strmap.hxx"
#include "HttpResponseHandler.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "istream/Handler.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "util/Compiler.h"

#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

struct Request final : IstreamHandler {
    bool cached = false;
    http_method_t method = HTTP_METHOD_GET;
    const char *uri;
    const char *request_headers;

    http_status_t status = HTTP_STATUS_OK;
    const char *response_headers;
    const char *response_body;

    bool eof = false;
    size_t body_read = 0;

    constexpr Request(const char *_uri, const char *_request_headers,
                      const char *_response_headers,
                      const char *_response_body) noexcept
        :uri(_uri), request_headers(_request_headers),
         response_headers(_response_headers),
         response_body(_response_body) {}

    /* virtual methods from class IstreamHandler */
    size_t OnData(gcc_unused const void *data, size_t length) noexcept override {
        EXPECT_LE(body_read + length, strlen(response_body));
        EXPECT_EQ(memcmp(response_body + body_read, data, length), 0);

        body_read += length;
        return length;
    }

    void OnEof() noexcept override {
        eof = true;
    }

    void OnError(std::exception_ptr) noexcept override {
        FAIL();
    }
};

#define DATE "Fri, 30 Jan 2009 10:53:30 GMT"
#define STAMP1 "Fri, 30 Jan 2009 08:53:30 GMT"
#define STAMP2 "Fri, 20 Jan 2009 08:53:30 GMT"
#define EXPIRES "Fri, 20 Jan 2029 08:53:30 GMT"

static Request requests[] = {
    { "/foo", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      "foo",
    },
    { "/foo", "x-foo: foo\n",
      "date: " DATE "\n"
      "last-modified: " STAMP2 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      "bar",
    },
    { "/query?string", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n",
      "foo",
    },
    { "/query?string2", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n",
      "foo",
    },
};

static HttpCache *cache;
static unsigned current_request;
static bool got_request, got_response;
static bool validated;

static StringMap *
parse_headers(struct pool &pool, const char *raw)
{
    if (raw == NULL)
        return NULL;

    GrowingBuffer gb;
    StringMap *headers = strmap_new(&pool);
    gb.Write(raw);
    header_parse_buffer(pool, *headers, std::move(gb));

    return headers;
}

static StringMap *
parse_request_headers(struct pool &pool, const Request &request)
{
    return parse_headers(pool, request.request_headers);
}

static StringMap *
parse_response_headers(struct pool &pool, const Request &request)
{
    return parse_headers(pool, request.response_headers);
}

class MyResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     sticky_hash_t sticky_hash,
                     const char *site_name,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     UnusedIstreamPtr body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) noexcept override;
};

void
MyResourceLoader::SendRequest(struct pool &pool,
                              sticky_hash_t,
                              gcc_unused const char *site_name,
                              http_method_t method,
                              gcc_unused const ResourceAddress &address,
                              gcc_unused http_status_t status,
                              StringMap &&headers,
                              UnusedIstreamPtr body,
                              gcc_unused const char *body_etag,
                              HttpResponseHandler &handler,
                              CancellablePointer &) noexcept
{
    const auto *request = &requests[current_request];
    StringMap *expected_rh;

    ASSERT_FALSE(got_request);
    ASSERT_FALSE(got_response);
    ASSERT_EQ(method, request->method);

    got_request = true;

    validated = headers.Get("if-modified-since") != nullptr;

    expected_rh = parse_request_headers(pool, *request);
    if (expected_rh != NULL) {
        for (const auto &i : headers) {
            const char *value = headers.Get(i.key);
            ASSERT_NE(value, nullptr);
            ASSERT_STREQ(value, i.value);
        }
    }

    body.Clear();

    StringMap response_headers(pool);
    if (request->response_headers != NULL) {
        GrowingBuffer gb;
        gb.Write(request->response_headers);

        header_parse_buffer(pool, response_headers, std::move(gb));
    }

    UnusedIstreamPtr response_body;
    if (request->response_body != NULL)
        response_body = istream_string_new(pool, request->response_body);

    handler.InvokeResponse(request->status,
                           std::move(response_headers),
                           std::move(response_body));
}

struct Context final : HttpResponseHandler {
    struct pool &pool;

    explicit Context(struct pool &_pool):pool(_pool) {}

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept override;
    void OnHttpError(std::exception_ptr ep) noexcept override;
};

void
Context::OnHttpResponse(http_status_t status, StringMap &&headers,
                        UnusedIstreamPtr body) noexcept
{
    Request *request = &requests[current_request];
    StringMap *expected_rh;

    ASSERT_EQ(status, request->status);

    expected_rh = parse_response_headers(pool, *request);
    if (expected_rh != NULL) {
        for (const auto &i : *expected_rh) {
            const char *value = headers.Get(i.key);
            ASSERT_NE(value, nullptr);
            ASSERT_STREQ(value, i.value);
        }
    }

    if (body) {
        request->body_read = 0;
        auto *b = body.Steal();
        b->SetHandler(*request);
        b->Read();
    }

    got_response = true;
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
{
    PrintException(ep);

    FAIL();
}

static void
run_cache_test(struct pool *root_pool, unsigned num, bool cached)
{
    const Request *request = &requests[num];
    struct pool *pool = pool_new_linear(root_pool, "t_http_cache", 8192);
    const auto uwa = MakeHttpAddress(request->uri).Host("foo");
    const ResourceAddress address(uwa);

    CancellablePointer cancel_ptr;

    current_request = num;

    StringMap headers(*pool);
    if (request->request_headers != NULL) {
        GrowingBuffer gb;
        gb.Write(request->request_headers);

        header_parse_buffer(*pool, headers, std::move(gb));
    }

    got_request = cached;
    got_response = false;

    Context context(*pool);
    http_cache_request(*cache, *pool, 0, nullptr,
                       request->method, address,
                       std::move(headers), nullptr,
                       context, cancel_ptr);
    pool_unref(pool);

    ASSERT_TRUE(got_request);
    ASSERT_TRUE(got_response);
}

TEST(HttpCache, Basic)
{
    const ScopeFbPoolInit fb_pool_init;
    PInstance instance;

    MyResourceLoader resource_loader;

    cache = http_cache_new(instance.root_pool, 1024 * 1024,
                           instance.event_loop, resource_loader);

    /* request one resource, cold and warm cache */
    run_cache_test(instance.root_pool, 0, false);
    run_cache_test(instance.root_pool, 0, true);

    /* another resource, different header */
    run_cache_test(instance.root_pool, 1, false);
    run_cache_test(instance.root_pool, 1, true);

    /* see if the first resource is still cached */
    run_cache_test(instance.root_pool, 0, true);

    /* see if the second resource is still cached */
    run_cache_test(instance.root_pool, 1, true);

    /* query string: should not be cached */

    run_cache_test(instance.root_pool, 2, false);

    validated = false;
    run_cache_test(instance.root_pool, 2, false);
    ASSERT_FALSE(validated);

    /* double check with a cacheable query string ("Expires" is
       set) */
    run_cache_test(instance.root_pool, 3, false);
    run_cache_test(instance.root_pool, 3, true);

    http_cache_close(cache);
}
