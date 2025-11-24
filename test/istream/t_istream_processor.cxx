// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "http/rl/FailingResourceLoader.hxx"
#include "translation/FailingService.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "widget/Widget.hxx"
#include "widget/View.hxx"
#include "widget/Ptr.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "http/Address.hxx"
#include "bp/XmlProcessor.hxx"
#include "widget/Inline.hxx"
#include "widget/Registry.hxx"
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
		cls->views.push_front(*NewFromPool<WidgetView>(pool, *address));
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
		    [[maybe_unused]] bool plain_text,
		    Widget &widget) noexcept
{
	widget.cls = MakeWidgetClass(widget.pool, widget.class_name);
	if (widget.cls != nullptr) {
		widget.from_request.view = widget.from_template.view = &widget.cls->views.front();
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
	static constexpr IstreamFilterTestOptions options{
		.expected_result = R"html(
foo &c:url;
<script><c:widget id="foo" type="bar"/></script>
bar
<b>http://localhost:8080/beng.html?%27%%22%3c%3e</b>

<META http-equiv="refresh" content="999;URL='/beng.html?&apos;%&quot;&lt;&gt;;focus=p&amp;path=refresh'">Refresh</meta>
<a href="/beng.html?&apos;%&quot;&lt;&gt;;focus=p&amp;path=relative">

)html",
	};

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
		FailingTranslationService translation_service;
		FailingResourceLoader resource_loader;
		WidgetRegistry widget_registry{pool, translation_service};

		auto ctx = SharedPoolPtr<WidgetContext>::Make
			(pool,
			 event_loop,
			 nullptr,
			 translation_service,
			 resource_loader, resource_loader,
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

INSTANTIATE_TYPED_TEST_SUITE_P(Processor, IstreamFilterTest,
			       IstreamProcessorTestTraits);
