// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StdioSink.hxx"
#include "FailingResourceLoader.hxx"
#include "PInstance.hxx"
#include "memory/fb_pool.hxx"
#include "bp/XmlProcessor.hxx"
#include "widget/Context.hxx"
#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Ptr.hxx"
#include "widget/RewriteUri.hxx"
#include "istream/OpenFileIstream.hxx"
#include "istream/istream_string.hxx"
#include "pool/SharedPtr.hxx"
#include "util/PrintException.hxx"
#include "stopwatch.hxx"

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
		   SharedPoolPtr<WidgetContext>, const StopwatchPtr &,
		   [[maybe_unused]] Widget &widget,
		   std::string_view,
		   [[maybe_unused]] RewriteUriMode mode,
		   [[maybe_unused]] bool stateful,
		   [[maybe_unused]] const char *view,
		   [[maybe_unused]] const struct escape_class *escape) noexcept
{
	return nullptr;
}

int
main(int argc, char **argv)
try {
	(void)argc;
	(void)argv;

	const ScopeFbPoolInit fb_pool_init;
	PInstance instance;

	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(instance.root_pool, instance.event_loop,
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

	auto result =
		processor_process(instance.root_pool, nullptr,
				  OpenFileIstream(instance.event_loop,
						  instance.root_pool,
						  "/dev/stdin"),
				  widget, std::move(ctx), PROCESSOR_CONTAINER);

	StdioSink sink(std::move(result));
	sink.LoopRead();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
