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

#include "IstreamFilterTest.hxx"
#include "FailingResourceLoader.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "http/Address.hxx"
#include "bp/XmlProcessor.hxx"
#include "widget/Inline.hxx"
#include "widget/Registry.hxx"
#include "bp/Global.hxx"
#include "util/ScopeExit.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <stdlib.h>
#include <stdio.h>

using std::string_view_literals::operator""sv;

class EventLoop;

const Event::Duration inline_widget_body_timeout = std::chrono::seconds(10);

static WidgetClass *
MakeWidgetClass(struct pool &pool, const char *name) noexcept
{

	if (StringIsEqual(name, "processed")) {
		auto *address = NewFromPool<HttpAddress>(pool, false,
							 "widget.server",
							 "/processed/");

		auto *cls = NewFromPool<WidgetClass>(pool);
		cls->views.address = *address;
		return cls;
	}

	return nullptr;
}

void
WidgetRegistry::LookupWidgetClass(struct pool &,
				  struct pool &widget_pool,
				  const char *name,
				  WidgetRegistryCallback callback,
				  CancellablePointer &) noexcept
{
	(void)translation_service; // suppress -Wunused-private-field

	callback(MakeWidgetClass(widget_pool, name));
}

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &stopwatch,
		    gcc_unused bool plain_text,
		    Widget &widget) noexcept
{
	widget.cls = MakeWidgetClass(widget.pool, widget.class_name);
	if (widget.cls != nullptr) {
		widget.from_request.view = widget.from_template.view = &widget.cls->views;
	}

	if (StringIsEqual(widget.class_name, "processed")) {
		auto body = istream_string_new(pool, R"html(
<META http-equiv="refresh" content="999;URL='refresh'">Refresh</meta>
<a href="relative">
)html"sv);

		return processor_process(pool, stopwatch,
					 std::move(body),
					 widget, ctx,
					 PROCESSOR_REWRITE_URL|PROCESSOR_FOCUS_WIDGET|PROCESSOR_PREFIX_XML_ID);
	}

	return istream_string_new(pool, p_strdup(&pool, widget.class_name));
}

class IstreamProcessorTestTraits {
public:
	static constexpr const char *expected_result = R"html(
foo &c:url;
<script><c:widget id="foo" type="bar"/></script>
bar
<b>http://localhost:8080/beng.html?%27%%22%3c%3e</b>

<META http-equiv="refresh" content="999;URL='/beng.html?&apos;%&quot;&lt;&gt;;focus=p&amp;path=refresh'">Refresh</meta>
<a href="/beng.html?&apos;%&quot;&lt;&gt;;focus=p&amp;path=relative">

)html";

	static constexpr bool call_available = true;
	static constexpr bool got_data_assert = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, R"html(
foo &c:url;
<script><c:widget id="foo" type="bar"/></script>
<c:widget id="foo" type="bar"/>
<b>&c:uri;</b>
<c:widget id="p" type="processed"/>
)html");
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		FailingResourceLoader resource_loader;
		WidgetRegistry widget_registry(pool, *(TranslationService *)(size_t)0x1);

		auto ctx = SharedPoolPtr<WidgetContext>::Make
			(pool,
			 event_loop, resource_loader, resource_loader,
			 &widget_registry,
			 nullptr, nullptr,
			 "localhost:8080",
			 "localhost:8080",
			 "/beng.html?'%\"<>",
			 "http://localhost:8080/beng.html?'%\"<>",
			 "/beng.html?'%\"<>"sv,
			 nullptr,
			 nullptr, nullptr, SessionId{}, nullptr,
			 nullptr);
		auto &widget = ctx->AddRootWidget(MakeRootWidget(pool,
								 nullptr));

		return processor_process(pool, nullptr,
					 std::move(input), widget,
					 std::move(ctx), PROCESSOR_CONTAINER);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(Processor, IstreamFilterTest,
			      IstreamProcessorTestTraits);
