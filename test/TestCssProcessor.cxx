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
#include "bp/session/Glue.hxx"
#include "bp/session/Session.hxx"
#include "widget/Inline.hxx"
#include "widget/Registry.hxx"
#include "bp/Global.hxx"
#include "util/ScopeExit.hxx"
#include "stopwatch.hxx"

#include <stdlib.h>
#include <stdio.h>

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
	static constexpr bool got_data_assert = true;
	static constexpr bool enable_blocking = true;
	static constexpr bool enable_abort_istream = true;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, input_text);
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		session_manager_init(event_loop, std::chrono::minutes(30), 0, 0);
		AtScopeExit() { session_manager_deinit(); };

		auto *session = session_new();

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
			 "/beng.html?'%\"<>",
			 nullptr,
			 "bp_session", session->id, "foo",
			 nullptr);
		auto &widget = ctx->AddRootWidget(MakeRootWidget(pool,
								 nullptr));

		session_put(session);

		return css_processor(pool, nullptr,
				     std::move(input), widget,
				     std::move(ctx),
				     CSS_PROCESSOR_REWRITE_URL);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(CssProcessor, IstreamFilterTest,
			      IstreamCssProcessorTestTraits);
