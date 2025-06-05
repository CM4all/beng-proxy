// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "http/rl/FailingResourceLoader.hxx"
#include "bp/XmlProcessor.hxx"
#include "bp/WidgetLookupProcessor.hxx"
#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/RewriteUri.hxx"
#include "istream/istream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

using std::string_view_literals::operator""sv;

/*
 * emulate missing libraries
 *
 */

UnusedIstreamPtr
embed_inline_widget(struct pool &pool,
		    SharedPoolPtr<WidgetContext>,
		    const StopwatchPtr &,
		    [[maybe_unused]] bool plain_text,
		    Widget &widget) noexcept
{
	const char *s = widget.GetIdPath();
	if (s == nullptr)
		s = "widget";

	return istream_string_new(pool, s);
}

RewriteUriMode
parse_uri_mode(std::string_view) noexcept
{
	return RewriteUriMode::DIRECT;
}

UnusedIstreamPtr
rewrite_widget_uri([[maybe_unused]] struct pool &pool,
		   SharedPoolPtr<WidgetContext> , const StopwatchPtr &,
		   [[maybe_unused]] Widget &widget,
		   [[maybe_unused]] std::string_view value,
		   [[maybe_unused]] RewriteUriMode mode,
		   [[maybe_unused]] bool stateful,
		   [[maybe_unused]] const char *view,
		   [[maybe_unused]] const struct escape_class *escape) noexcept
{
	return nullptr;
}

/*
 * WidgetLookupHandler
 *
 */

class MyWidgetLookupHandler final : public WidgetLookupHandler {
public:
	/* virtual methods from class WidgetLookupHandler */
	void WidgetFound([[maybe_unused]] Widget &widget) noexcept override {
		fprintf(stderr, "widget found\n");
	}

	void WidgetNotFound() noexcept override {
		fprintf(stderr, "widget not found\n");
	}

	void WidgetLookupError(std::exception_ptr ep) noexcept override {
		PrintException(ep);
	}
};

/*
 * tests
 *
 */

TEST(Processor, Abort)
{
	TestInstance instance;

	auto pool = pool_new_libc(instance.root_pool, "test");

	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(*pool, instance.event_loop,
		 resource_loader, resource_loader,
		 nullptr,
		 nullptr, nullptr,
		 "localhost:8080",
		 "localhost:8080",
		 "/beng.html",
		 "http://localhost:8080/beng.html",
		 "/beng.html"sv,
		 nullptr,
		 nullptr, nullptr, SessionId{}, nullptr,
		 nullptr);
	auto &widget = ctx->AddRootWidget(MakeRootWidget(instance.root_pool,
							 nullptr));

	CancellablePointer cancel_ptr;
	MyWidgetLookupHandler handler;
	processor_lookup_widget(*pool, nullptr, istream_block_new(*pool),
				widget, "foo", std::move(ctx),
				PROCESSOR_CONTAINER,
				handler, cancel_ptr);

	cancel_ptr.Cancel();

	pool.reset();
}
