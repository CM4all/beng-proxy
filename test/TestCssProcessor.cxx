// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
#include "bp/CssProcessor.hxx"
#include "bp/session/Id.hxx"
#include "widget/Inline.hxx"
#include "widget/Registry.hxx"
#include "bp/Global.hxx"
#include "util/ScopeExit.hxx"
#include "stopwatch.hxx"

#include <stdlib.h>
#include <stdio.h>

using std::string_view_literals::operator""sv;

class EventLoop;

const Event::Duration inline_widget_body_timeout = std::chrono::seconds(10);

void
WidgetRegistry::LookupWidgetClass(struct pool &,
				  struct pool &,
				  const char *,
				  WidgetRegistryCallback callback,
				  CancellablePointer &) noexcept
{
	(void)translation_service; // suppress -Wunused-private-field

	callback(nullptr);
}

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, SharedPoolPtr<WidgetContext>,
		    const StopwatchPtr &,
		    gcc_unused bool plain_text,
		    Widget &widget) noexcept
{
	return istream_string_new(pool, p_strdup(&pool, widget.class_name));
}

class IstreamCssProcessorTestTraits {
public:
	static constexpr const char *const input_text =
		"body {\n"
		"  font-family: serif;\n"
		"  -c-mode: partial;\n"
		"  background-image: url(foo.jpg);\n"
		"}\n";

	static constexpr const char *const expected_result =
		"body {\n"
		"  font-family: serif;\n"
		"  \n"
		"  background-image: url(foo.jpg);\n"
		"}\n";

	static constexpr bool call_available = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, input_text);
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

		return css_processor(pool, nullptr,
				     std::move(input), widget,
				     std::move(ctx),
				     CSS_PROCESSOR_REWRITE_URL);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(CssProcessor, IstreamFilterTest,
			      IstreamCssProcessorTestTraits);
