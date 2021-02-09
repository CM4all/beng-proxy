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

#include "tconstruct.hxx"
#include "http_cache.hxx"
#include "ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "RecordingHttpResponseHandler.hxx"
#include "http/Address.hxx"
#include "GrowingBuffer.hxx"
#include "http/HeaderParser.hxx"
#include "strmap.hxx"
#include "http/ResponseHandler.hxx"
#include "PInstance.hxx"
#include "fb_pool.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "util/Cancellable.hxx"
#include "util/Compiler.h"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

struct Request final {
	http_method_t method = HTTP_METHOD_GET;
	const char *uri;
	const char *request_headers;

	http_status_t status = HTTP_STATUS_OK;
	const char *response_headers;
	const char *response_body;

	constexpr Request(const char *_uri, const char *_request_headers,
			  const char *_response_headers,
			  const char *_response_body) noexcept
		:uri(_uri), request_headers(_request_headers),
		 response_headers(_response_headers),
		 response_body(_response_body) {}
};

#define DATE "Fri, 30 Jan 2009 10:53:30 GMT"
#define STAMP1 "Fri, 30 Jan 2009 08:53:30 GMT"
#define STAMP2 "Fri, 20 Jan 2009 08:53:30 GMT"
#define EXPIRES "Fri, 20 Jan 2029 08:53:30 GMT"

static constexpr Request requests[] = {
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
	const Request *current_request;
	bool got_request;
	bool validated;

	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 sticky_hash_t sticky_hash,
			 const char *cache_tag,
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
			      const StopwatchPtr &,
			      sticky_hash_t,
			      gcc_unused const char *cache_tag,
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
	const auto *request = current_request;
	ASSERT_NE(request, nullptr);

	ASSERT_FALSE(got_request);
	ASSERT_EQ(method, request->method);

	got_request = true;

	validated = headers.Get("if-modified-since") != nullptr;

	auto *expected_rh = parse_request_headers(pool, *request);
	if (expected_rh != NULL) {
		for (const auto &i : headers) {
			const char *value = headers.Get(i.key);
			ASSERT_NE(value, nullptr);
			ASSERT_STREQ(value, i.value);
		}
	}

	body.Clear();

	StringMap response_headers;
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

struct Instance final : PInstance {
	MyResourceLoader resource_loader;

	HttpCache *const cache;

	Instance()
		:cache(http_cache_new(root_pool, 1024 * 1024, true,
				      event_loop, resource_loader))
	{
	}

	~Instance() noexcept {
		http_cache_close(cache);
	}
};

static void
run_cache_test(Instance &instance, const Request &request, bool cached)
{
	auto pool = pool_new_linear(instance.root_pool, "t_http_cache", 8192);
	const AllocatorPtr alloc{pool};
	const auto uwa = MakeHttpAddress(request.uri).Host("foo");
	const ResourceAddress address(uwa);

	CancellablePointer cancel_ptr;

	instance.resource_loader.current_request = cached ? nullptr : &request;

	StringMap headers;
	if (request.request_headers != NULL) {
		GrowingBuffer gb;
		gb.Write(request.request_headers);

		header_parse_buffer(alloc, headers, std::move(gb));
	}

	instance.resource_loader.got_request = false;

	RecordingHttpResponseHandler handler(instance.root_pool,
					     instance.event_loop);

	http_cache_request(*instance.cache, pool, nullptr,
			   0, nullptr, nullptr,
			   request.method, address,
			   std::move(headers), nullptr,
			   handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Dispatch();

	ASSERT_NE(instance.resource_loader.got_request, cached);
	ASSERT_FALSE(handler.IsAlive());
	ASSERT_EQ(handler.error, nullptr);

	const auto *expected_rh = parse_response_headers(pool, request);
	if (expected_rh != nullptr) {
		for (const auto &i : *expected_rh) {
			auto h = handler.headers.equal_range(i.key);
			ASSERT_NE(h.first, h.second);

			auto j = std::find_if(h.first, h.second, [&i](const auto &p){
				return p.second == i.value;
			});

			ASSERT_NE(j, h.second);
			handler.headers.erase(j);
		}

		ASSERT_TRUE(handler.headers.empty());
	}

	if (request.response_body != nullptr) {
		ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::END);
		ASSERT_STREQ(handler.body.c_str(), request.response_body);
	} else {
		ASSERT_EQ(handler.state, RecordingHttpResponseHandler::State::NO_BODY);
	}
}

TEST(HttpCache, Basic)
{
	const ScopeFbPoolInit fb_pool_init;
	Instance instance;

	/* request one resource, cold and warm cache */
	run_cache_test(instance, requests[0], false);
	run_cache_test(instance, requests[0], true);

	/* another resource, different header */
	run_cache_test(instance, requests[1], false);
	run_cache_test(instance, requests[1], true);

	/* see if the first resource is still cached */
	run_cache_test(instance, requests[0], true);

	/* see if the second resource is still cached */
	run_cache_test(instance, requests[1], true);

	/* query string: should not be cached */

	run_cache_test(instance, requests[2], false);

	instance.resource_loader.validated = false;
	run_cache_test(instance, requests[2], false);
	ASSERT_FALSE(instance.resource_loader.validated);

	/* double check with a cacheable query string ("Expires" is
	   set) */
	run_cache_test(instance, requests[3], false);
	run_cache_test(instance, requests[3], true);
}

TEST(HttpCache, MultiVary)
{
	const ScopeFbPoolInit fb_pool_init;
	Instance instance;

	/* request one resource, cold and warm cache */
	static constexpr Request r0{
		"/foo", nullptr,
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"1",
	};

	run_cache_test(instance, r0, false);
	run_cache_test(instance, r0, true);

	/* another resource, different header 1 */
	static constexpr Request r1{
		"/foo",
		"x-foo: foo\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"2",
	};

	run_cache_test(instance, r1, false);
	run_cache_test(instance, r1, true);

	/* another resource, different header 2 */
	static constexpr Request r2{
		"/foo",
		"x-bar: bar\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"3",
	};

	run_cache_test(instance, r2, false);
	run_cache_test(instance, r2, true);

	/* another resource, different header 3 */
	static constexpr Request r3{
		"/foo",
		"x-abc: abc\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"4",
	};

	run_cache_test(instance, r3, false);
	run_cache_test(instance, r3, true);

	/* another resource, different header combined 1+2 */
	static constexpr Request r4{
		"/foo",
		"x-foo: foo\n"
		"x-abc: abc\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"5",
	};

	run_cache_test(instance, r4, false);
	run_cache_test(instance, r4, true);

	/* another resource, different header combined 2+3 */
	static constexpr Request r5{
		"/foo",
		"x-bar: bar\n"
		"x-abc: abc\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"5",
	};

	run_cache_test(instance, r5, false);
	run_cache_test(instance, r5, true);

	static constexpr Request r5b{
		"/foo",
		"x-abc: abc\n"
		"x-bar: bar\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"5",
	};

	run_cache_test(instance, r5b, true);

	/* another resource, different header 1 value */
	static constexpr Request r6{
		"/foo",
		"x-foo: xyz\n",
		"date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		"6",
	};

	run_cache_test(instance, r6, false);
	run_cache_test(instance, r6, true);

	/* check all cache items again */
	run_cache_test(instance, r1, true);
	run_cache_test(instance, r2, true);
	run_cache_test(instance, r3, true);
	run_cache_test(instance, r4, true);
	run_cache_test(instance, r5, true);
	run_cache_test(instance, r5b, true);
	run_cache_test(instance, r6, true);
}
