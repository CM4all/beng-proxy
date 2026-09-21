// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "TestInstance.hxx"
#include "RecordingStringSinkHandler.hxx"
#include "http/rl/FailingResourceLoader.hxx"
#include "translation/FailingService.hxx"
#include "bp/XmlProcessor.hxx"
#include "bp/WidgetLookupProcessor.hxx"
#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/Class.hxx"
#include "widget/View.hxx"
#include "widget/Context.hxx"
#include "widget/LookupHandler.hxx"
#include "widget/RewriteUri.hxx"
#include "istream/istream.hxx"
#include "istream/BlockIstream.hxx"
#include "istream/StringSink.hxx"
#include "istream/istream_string.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <string>

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

static constexpr auto rewritten_uri = "REWRITTEN"sv;

UnusedIstreamPtr
rewrite_widget_uri(struct pool &pool,
		   SharedPoolPtr<WidgetContext> , const StopwatchPtr &,
		   [[maybe_unused]] Widget &widget,
		   [[maybe_unused]] std::string_view value,
		   [[maybe_unused]] RewriteUriMode mode,
		   [[maybe_unused]] bool stateful,
		   [[maybe_unused]] const char *view,
		   [[maybe_unused]] const struct escape_class *escape) noexcept
{
	return istream_string_new(pool, rewritten_uri);
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

	FailingTranslationService translation_service;
	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(*pool, instance.event_loop,
		 nullptr,
		 translation_service,
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

/**
 * Run the XML processor on the given template and return its output.
 *
 * The container widget is a child of the root widget, because
 * rewriting URIs and prefixing XML ids both require a widget which is
 * not the root.
 */
static std::string
Process(std::string_view html, unsigned options)
{
	TestInstance instance;

	auto pool = pool_new_libc(instance.root_pool, "test");

	FailingTranslationService translation_service;
	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(*pool, instance.event_loop,
		 nullptr,
		 translation_service,
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
	auto &root_widget = ctx->AddRootWidget(MakeRootWidget(instance.root_pool,
							      nullptr));

	static WidgetView view{nullptr};
	static WidgetClass cls;

	auto container = MakeWidget(instance.root_pool, nullptr);
	container->parent = &root_widget;
	container->SetId("foo"sv);
	container->SetClassName("bar"sv);
	container->cls = &cls;
	container->from_template.view = container->from_request.view = &view;
	root_widget.children.push_front(*container.release());

	RecordingStringSinkHandler handler;
	auto &sink =
		NewStringSink(*pool,
			      processor_process(*pool, nullptr,
						istream_string_new(*pool, html),
						root_widget.children.front(),
						std::move(ctx), options),
			      handler, handler.cancel_ptr);
	ReadStringSink(sink);

	instance.event_loop.Run();

	auto result = std::move(handler).TakeValue();
	pool.reset();
	return result;
}

static constexpr unsigned rewrite_and_prefix_options =
	PROCESSOR_REWRITE_URL|PROCESSOR_FOCUS_WIDGET|PROCESSOR_PREFIX_XML_ID;

/**
 * An "id" attribute before the URI attribute is prefixed (control
 * group for AnchorNameAfterHref).
 */
TEST(Processor, AnchorNameBeforeHref)
{
	EXPECT_EQ(Process(R"(<a name="___y" href="/x">z</a>)"sv,
			  rewrite_and_prefix_options),
		  R"(<a name="C_foo__y" href="REWRITTEN">z</a>)"sv);
}

/**
 * The "name" attribute of an anchor which follows the "href"
 * attribute must not be edited: its substitution would be added to
 * the #ReplaceIstream before the (postponed) "href" substitution,
 * which is out of order, and used to make #ReplaceIstream loop
 * forever.
 */
TEST(Processor, AnchorNameAfterHref)
{
	EXPECT_EQ(Process(R"(<a href="/x" name="___y">z</a>)"sv,
			  rewrite_and_prefix_options),
		  R"(<a href="REWRITTEN" name="___y">z</a>)"sv);
}
