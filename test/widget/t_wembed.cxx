// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "../TestInstance.hxx"
#include "../FailingResourceLoader.hxx"
#include "widget/Inline.hxx"
#include "widget/Widget.hxx"
#include "widget/Request.hxx"
#include "widget/Resolver.hxx"
#include "widget/Context.hxx"
#include "bp/XmlProcessor.hxx"
#include "uri/Dissect.hxx"
#include "http/ResponseHandler.hxx"
#include "istream/istream.hxx"
#include "istream/istream_iconv.hxx"
#include "pool/pool.hxx"
#include "pool/SharedPtr.hxx"
#include "bp/session/Lease.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

using std::string_view_literals::operator""sv;

const char *
Widget::GetLogName() const noexcept
{
	return "dummy";
}

std::string_view
Widget::LoggerDomain::GetDomain() const noexcept
{
	return "dummy";
}

UnusedIstreamPtr
istream_iconv_new([[maybe_unused]] struct pool &pool, UnusedIstreamPtr input,
		  [[maybe_unused]] const char *tocode,
		  [[maybe_unused]] const char *fromcode) noexcept
{
	return input;
}

void
Widget::DiscardForFocused() noexcept
{
}

void
Widget::Cancel() noexcept
{
}

void
Widget::CheckHost(const char *, const char *) const
{
}

RealmSessionLease
WidgetContext::GetRealmSession() const
{
	return nullptr;
}

void
RealmSessionLease::Put(SessionManager &, RealmSession &) noexcept
{
}

void
Widget::LoadFromSession([[maybe_unused]] RealmSession &session) noexcept
{
}

void
widget_http_request([[maybe_unused]] struct pool &pool,
		    [[maybe_unused]] Widget &widget,
		    SharedPoolPtr<WidgetContext>,
		    const StopwatchPtr &,
		    HttpResponseHandler &handler,
		    [[maybe_unused]] CancellablePointer &cancel_ptr) noexcept
{
	handler.InvokeError(std::make_exception_ptr(std::runtime_error("Test")));
}

struct TestOperation final : Cancellable {
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
	}
};

void
ResolveWidget(AllocatorPtr alloc,
	      [[maybe_unused]] Widget &widget,
	      WidgetRegistry &,
	      [[maybe_unused]] WidgetResolverCallback callback,
	      CancellablePointer &cancel_ptr) noexcept
{
	auto to = alloc.New<TestOperation>();
	cancel_ptr = *to;
}

static void
test_abort_resolver()
{
	TestInstance instance;
	const char *uri;
	bool ret;
	DissectedUri dissected_uri;

	FailingResourceLoader resource_loader;

	auto pool = pool_new_linear(instance.root_pool, "test", 4096);

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

	uri = "/beng.html";
	ret = dissected_uri.Parse(uri);
	if (!ret) {
		fprintf(stderr, "uri_parse() failed\n");
		exit(2);
	}

	const auto root_widget = MakeRootWidget(pool, "foo");
	Widget widget(pool, nullptr);
	widget.parent = root_widget.get();

	auto istream = embed_inline_widget(*pool, std::move(ctx),
					   nullptr, false, widget);
}

int
main(int, char **)
{
	test_abort_resolver();
}
