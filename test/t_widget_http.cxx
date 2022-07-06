/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "widget/Request.hxx"
#include "widget/LookupHandler.hxx"
#include "http/Address.hxx"
#include "strmap.hxx"
#include "http/HeaderParser.hxx"
#include "PInstance.hxx"
#include "ResourceLoader.hxx"
#include "http/ResponseHandler.hxx"
#include "bp/XmlProcessor.hxx"
#include "bp/CssProcessor.hxx"
#include "bp/TextProcessor.hxx"
#include "translation/Transformation.hxx"
#include "translation/SuffixRegistry.hxx"
#include "translation/AddressSuffixRegistry.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "istream/AutoPipeIstream.hxx"
#include "pool/SharedPtr.hxx"
#include "bp/session/Lease.hxx"
#include "bp/session/Manager.hxx"
#include "bp/session/Session.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

struct request {
	bool cached;
	http_method_t method;
	const char *uri;
	const char *request_headers;

	http_status_t status;
	const char *response_headers;
	const char *response_body;
};

static unsigned test_id;
static bool got_request, got_response;

bool
processable(const StringMap &) noexcept
{
	return false;
}

UnusedIstreamPtr
processor_process(struct pool &,
		  const StopwatchPtr &,
		  UnusedIstreamPtr istream,
		  Widget &,
		  SharedPoolPtr<WidgetContext>,
		  unsigned) noexcept
{
	return istream;
}

void
processor_lookup_widget(struct pool &,
			const StopwatchPtr &,
			UnusedIstreamPtr,
			Widget &,
			const char *,
			SharedPoolPtr<WidgetContext>,
			unsigned,
			WidgetLookupHandler &handler,
			CancellablePointer &)
{
	handler.WidgetNotFound();
}

UnusedIstreamPtr
css_processor(struct pool &,
	      const StopwatchPtr &,
	      UnusedIstreamPtr stream,
	      Widget &,
	      SharedPoolPtr<WidgetContext>,
	      unsigned) noexcept
{
	return stream;
}

bool
text_processor_allowed(const StringMap &) noexcept
{
	return false;
}

UnusedIstreamPtr
text_processor(struct pool &, UnusedIstreamPtr stream,
	       const Widget &,
	       const WidgetContext &) noexcept
{
	return stream;
}

bool
suffix_registry_lookup(AllocatorPtr,
		       TranslationService &,
		       const ResourceAddress &,
		       const StopwatchPtr &,
		       SuffixRegistryHandler &,
		       CancellablePointer &) noexcept
{
	return false;
}

TranslationService *global_translation_service;

class PipeStock;
PipeStock *global_pipe_stock;

UnusedIstreamPtr
NewAutoPipeIstream(struct pool *, UnusedIstreamPtr input,
		   PipeStock *) noexcept
{
	return input;
}

class MyResourceLoader final : public ResourceLoader {
public:
	/* virtual methods from class ResourceLoader */
	void SendRequest(struct pool &pool,
			 const StopwatchPtr &parent_stopwatch,
			 const ResourceRequestParams &params,
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
			      const ResourceRequestParams &,
			      http_method_t method,
			      const ResourceAddress &,
			      http_status_t,
			      StringMap &&headers,
			      UnusedIstreamPtr body,
			      const char *,
			      HttpResponseHandler &handler,
			      CancellablePointer &) noexcept
{
	StringMap response_headers;
	const char *p;

	EXPECT_FALSE(got_request);
	ASSERT_EQ(method, HTTP_METHOD_GET);
	EXPECT_FALSE(body);

	got_request = true;

	body.Clear();

	switch (test_id) {
	case 0:
		p = headers.Get("cookie");
		EXPECT_EQ(p, nullptr);

		/* set one cookie */
		response_headers.Add(pool, "set-cookie", "foo=bar");
		break;

	case 1:
		/* is the one cookie present? */
		p = headers.Get("cookie");
		EXPECT_NE(p, nullptr);
		ASSERT_STREQ(p, "foo=bar");

		/* add 2 more cookies */
		response_headers.Add(pool, "set-cookie", "a=b, c=d");
		break;

	case 2:
		/* are 3 cookies present? */
		p = headers.Get("cookie");
		EXPECT_NE(p, nullptr);
		ASSERT_STREQ(p, "c=d; a=b; foo=bar");

		/* set two cookies in two headers */
		response_headers.Add(pool, "set-cookie", "e=f");
		response_headers.Add(pool, "set-cookie", "g=h");
		break;

	case 3:
		/* check for 5 cookies */
		p = headers.Get("cookie");
		EXPECT_NE(p, nullptr);
		ASSERT_STREQ(p, "g=h; e=f; c=d; a=b; foo=bar");
		break;
	}

	handler.InvokeResponse(HTTP_STATUS_OK,
			       std::move(response_headers),
			       istream_null_new(pool));
}

struct Context final : HttpResponseHandler {
	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(http_status_t status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept override;
	void OnHttpError(std::exception_ptr ep) noexcept override;
};

void
Context::OnHttpResponse(http_status_t status, StringMap &&,
			UnusedIstreamPtr body) noexcept
{
	EXPECT_FALSE(got_response);
	ASSERT_EQ(status, 200);
	ASSERT_TRUE(body);

	got_response = true;
}

void
Context::OnHttpError(std::exception_ptr ep) noexcept
{
	PrintException(ep);

	FAIL();
}

TEST(WidgetHttpTest, CookieClient)
{
	PInstance instance;
	struct pool *pool = instance.root_pool;

	SessionManager session_manager(instance.event_loop,
				       std::chrono::minutes(30),
				       0, 0);

	const auto address = MakeHttpAddress("/bar/").Host("foo");
	WidgetClass cls;
	cls.views.address = address;
	cls.stateful = true;

	CancellablePointer cancel_ptr;

	const auto session_id = session_manager.CreateSession()->id;

	MyResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(*pool, instance.event_loop,
		 resource_loader, resource_loader,
		 nullptr,
		 nullptr, nullptr,
		 "localhost", "localhost",
		 nullptr, nullptr,
		 std::string_view{},
		 nullptr,
		 &session_manager,
		 nullptr,
		 session_id,
		 "foo",
		 strmap_new(pool));

	Widget widget(*pool, &cls);

	for (test_id = 0; test_id < 4; ++test_id) {
		got_request = false;
		got_response = false;

		Context context;
		widget_http_request(*pool, widget, ctx,
				    nullptr,
				    context, cancel_ptr);

		assert(got_request);
		assert(got_response);
	}
}
