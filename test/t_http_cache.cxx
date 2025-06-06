// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "tconstruct.hxx"
#include "http/cache/Public.hxx"
#include "http/rl/ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "RecordingHttpResponseHandler.hxx"
#include "DeferHttpResponseHandler.hxx"
#include "http/Address.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/HeaderParser.hxx"
#include "http/Method.hxx"
#include "strmap.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "util/Cancellable.hxx"
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
	const char *tag = nullptr;

	HttpMethod method = HttpMethod::GET;
	const char *uri;
	const char *request_headers;

	HttpStatus status = HttpStatus::OK;
	const char *response_headers;
	const char *response_body;

	bool auto_flush_cache = false;
};

#define DATE "Fri, 30 Jan 2009 10:53:30 GMT"
#define STAMP1 "Fri, 30 Jan 2009 08:53:30 GMT"
#define STAMP2 "Fri, 20 Jan 2009 08:53:30 GMT"
#define EXPIRES "Fri, 20 Jan 2029 08:53:30 GMT"

static constexpr Request requests[] = {
	{
		.uri = "/foo",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n",
		.response_body = "foo",
	},
	{
		.uri = "/foo",
		.request_headers = "x-foo: foo\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP2 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n",
		.response_body = "bar",
	},
	{
		.uri = "/query?string",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n",
		.response_body = "foo",
	},
	{
		.uri = "/query?string2",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n",
		.response_body = "foo",
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
			 const ResourceRequestParams &params,
			 HttpMethod method,
			 const ResourceAddress &address,
			 StringMap &&headers,
			 UnusedIstreamPtr body,
			 HttpResponseHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};

void
MyResourceLoader::SendRequest(struct pool &pool,
			      const StopwatchPtr &,
			      const ResourceRequestParams &,
			      HttpMethod method,
			      const ResourceAddress &,
			      StringMap &&headers,
			      UnusedIstreamPtr body,
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

struct Instance final : TestInstance {
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
	auto uwa = MakeHttpAddress(request.uri).Host("foo");
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

	DeferHttpResponseHandler defer_handler(instance.root_pool,
					       instance.event_loop,
					       handler);

	http_cache_request(*instance.cache, pool, nullptr,
			   {
				   .auto_flush_cache = request.auto_flush_cache,
				   .cache_tag = request.tag,
			   },
			   request.method, address,
			   std::move(headers), nullptr,
			   defer_handler, cancel_ptr);

	if (handler.IsAlive())
		instance.event_loop.Run();

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

	/* with "Cache-Control:no-cache"; not served from cache, but
	   updates the cache */
	auto no_cache_request = requests[0];
	no_cache_request.request_headers = "cache-control: no-cache\n";
	no_cache_request.response_body = "new_foo";

	run_cache_test(instance, no_cache_request, false);

	auto verify_no_cache_request = no_cache_request;
	verify_no_cache_request.request_headers = nullptr;
	run_cache_test(instance, verify_no_cache_request, true);

	/* didn't flush the other "vary" cache item at the same URI */
	run_cache_test(instance, requests[1], true);
}

TEST(HttpCache, CacheableWithoutResponseBody)
{
	Instance instance;

	static constexpr Request r0{
		.uri = "/cacheable-no-response-body",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n",
	};

	run_cache_test(instance, r0, false);
	run_cache_test(instance, r0, true);
}

TEST(HttpCache, Uncacheable)
{
	Instance instance;

	static constexpr Request with_body{
		.uri = "/uncacheable-body",
		.response_headers = "date: " DATE "\n"
		"cache-control: no-cache\n",
		.response_body = "foo",
	};

	run_cache_test(instance, with_body, false);
	run_cache_test(instance, with_body, false);

	static constexpr Request no_body{
		.uri = "/uncacheable-empty",
		.response_headers = "date: " DATE "\n"
		"cache-control: no-cache\n",
	};

	run_cache_test(instance, no_body, false);
	run_cache_test(instance, no_body, false);
}

TEST(HttpCache, MultiVary)
{
	Instance instance;

	/* request one resource, cold and warm cache */
	static constexpr Request r0{
		.uri = "/foo",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "1",
	};

	run_cache_test(instance, r0, false);
	run_cache_test(instance, r0, true);

	/* another resource, different header 1 */
	static constexpr Request r1{
		.uri = "/foo",
		.request_headers = "x-foo: foo\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "2",
	};

	run_cache_test(instance, r1, false);
	run_cache_test(instance, r1, true);

	/* another resource, different header 2 */
	static constexpr Request r2{
		.uri = "/foo",
		.request_headers = "x-bar: bar\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "3",
	};

	run_cache_test(instance, r2, false);
	run_cache_test(instance, r2, true);

	/* another resource, different header 3 */
	static constexpr Request r3{
		.uri = "/foo",
		.request_headers = "x-abc: abc\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "4",
	};

	run_cache_test(instance, r3, false);
	run_cache_test(instance, r3, true);

	/* another resource, different header combined 1+2 */
	static constexpr Request r4{
		.uri = "/foo",
		.request_headers = "x-foo: foo\n"
		"x-abc: abc\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "5",
	};

	run_cache_test(instance, r4, false);
	run_cache_test(instance, r4, true);

	/* another resource, different header combined 2+3 */
	static constexpr Request r5{
		.uri = "/foo",
		.request_headers = "x-bar: bar\n"
		"x-abc: abc\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "5",
	};

	run_cache_test(instance, r5, false);
	run_cache_test(instance, r5, true);

	static constexpr Request r5b{
		.uri = "/foo",
		.request_headers = "x-abc: abc\n"
		"x-bar: bar\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "5",
	};

	run_cache_test(instance, r5b, true);

	/* another resource, different header 1 value */
	static constexpr Request r6{
		.uri = "/foo",
		.request_headers = "x-foo: xyz\n",
		.response_headers = "date: " DATE "\n"
		"last-modified: " STAMP1 "\n"
		"expires: " EXPIRES "\n"
		"vary: x-foo\n"
		"vary: x-bar, x-abc\n",
		.response_body = "6",
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

TEST(HttpCache, Tag)
{
	Instance instance;

	Request request = requests[0];
	request.tag = "abc";

	run_cache_test(instance, request, false);
	run_cache_test(instance, request, true);

	/* this does not flush the item */
	http_cache_flush_tag(*instance.cache, "def");
	run_cache_test(instance, request, true);

	/* but this does */
	http_cache_flush_tag(*instance.cache, "abc");
	run_cache_test(instance, request, false);
	run_cache_test(instance, request, true);

	/* AUTO_FLUSH_CACHE test (GET does not flush) */

	Request r2{
		.tag = request.tag,
		.uri = "/bar",
		.response_headers = "",
		.response_body = "bar",
		.auto_flush_cache = true,
	};

	run_cache_test(instance, r2, false);
	run_cache_test(instance, request, true);

	/* AUTO_FLUSH_CACHE test (unsuccessful POST does not flush) */

	r2.method = HttpMethod::POST;
	r2.status = HttpStatus::FORBIDDEN;

	run_cache_test(instance, r2, false);
	run_cache_test(instance, request, true);

	/* AUTO_FLUSH_CACHE test (successful POST flushes) */

	r2.status = HttpStatus::OK;

	run_cache_test(instance, r2, false);
	run_cache_test(instance, request, false);
	run_cache_test(instance, request, true);
}
