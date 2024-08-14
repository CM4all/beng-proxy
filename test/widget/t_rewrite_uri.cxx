// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/rl/FailingResourceLoader.hxx"
#include "../tconstruct.hxx"
#include "widget/RewriteUri.hxx"
#include "http/Address.hxx"
#include "bp/session/Lease.hxx"
#include "bp/session/Session.hxx"
#include "bp/session/Manager.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "widget/Resolver.hxx"
#include "PInstance.hxx"
#include "escape/Pool.hxx"
#include "escape/HTML.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "istream/StringSink.hxx"
#include "pool/SharedPtr.hxx"
#include "widget/Inline.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"

#include <gtest/gtest.h>

using std::string_view_literals::operator""sv;

const Event::Duration inline_widget_body_timeout = std::chrono::seconds(10);

struct MakeWidgetClass : WidgetClass {
	explicit MakeWidgetClass(struct pool &p, const char *uri) {
		auto http = MakeHttpAddress(uri).Host("widget-server");
		views.push_front(*NewFromPool<WidgetView>(p, *NewFromPool<HttpAddress>(p, p, http)));
	}
};


/*
 * dummy implementations to satisfy the linker
 *
 */

RealmSessionLease
WidgetContext::GetRealmSession() const
{
	return nullptr;
}

RealmSession *
Session::GetRealm(std::string_view) noexcept
{
	return nullptr;
}

SessionLease
SessionManager::Find(SessionId) noexcept
{
	return nullptr;
}

void
SessionManager::Put(Session &) noexcept
{
}

void
Widget::LoadFromSession([[maybe_unused]] RealmSession &session) noexcept
{
}

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, SharedPoolPtr<WidgetContext>,
		    const StopwatchPtr &,
		    [[maybe_unused]] bool plain_text,
		    Widget &widget) noexcept
{
	return istream_string_new(pool, widget.class_name);
}

/*
 * A dummy resolver
 *
 */

void
ResolveWidget(AllocatorPtr,
	      Widget &widget,
	      WidgetRegistry &,
	      WidgetResolverCallback callback,
	      [[maybe_unused]] CancellablePointer &cancel_ptr) noexcept
{

	if (strcmp(widget.class_name, "1") == 0) {
		widget.cls = NewFromPool<MakeWidgetClass>(widget.pool, widget.pool, "/1/");
	} else if (strcmp(widget.class_name, "2") == 0) {
		widget.cls = NewFromPool<MakeWidgetClass>(widget.pool, widget.pool, "/2");
	} else if (strcmp(widget.class_name, "3") == 0) {
		auto *cls = NewFromPool<MakeWidgetClass>(widget.pool, widget.pool, "/3");
		cls->local_uri = "/resources/3/";
		widget.cls = cls;
	} else if (strcmp(widget.class_name, "untrusted_host") == 0) {
		auto *cls = NewFromPool<MakeWidgetClass>(widget.pool, widget.pool, "/1/");
		cls->untrusted_host = "untrusted.host";
		widget.cls = cls;
	} else if (strcmp(widget.class_name, "untrusted_raw_site_suffix") == 0) {
		auto *cls = NewFromPool<MakeWidgetClass>(widget.pool, widget.pool, "/1/");
		cls->untrusted_raw_site_suffix = "_urss";
		widget.cls = cls;
	}

	if (widget.cls != NULL)
		widget.from_template.view = widget.from_request.view =
			&widget.cls->views.front();

	callback();
}


/*
 * Check utilities
 *
 */

namespace {

struct MyStringSinkHandler final : StringSinkHandler {
	std::string value;
	bool finished = false;

	void OnStringSinkSuccess(std::string &&_value) noexcept override {
		value = std::move(_value);
		finished = true;
	}

	void OnStringSinkError(std::exception_ptr) noexcept override {
		finished = true;
	}
};

}

static void
assert_istream_equals(struct pool *pool, UnusedIstreamPtr _istream,
		      const char *value)
{
	MyStringSinkHandler ctx;
	CancellablePointer cancel_ptr;

	ASSERT_TRUE(_istream);
	ASSERT_NE(value, nullptr);

	auto &sink = NewStringSink(*pool, std::move(_istream),
				   ctx, cancel_ptr);

	while (!ctx.finished)
		ReadStringSink(sink);

	ASSERT_STREQ(ctx.value.c_str(), value);
}

static void
assert_rewrite_check4(EventLoop &event_loop,
		      struct pool *widget_pool, const char *site_name,
		      Widget *widget,
		      const char *value, RewriteUriMode mode, bool stateful,
		      const char *view,
		      const char *result)
{
	auto pool = pool_new_libc(widget_pool, "rewrite");

	std::string_view value2{};
	if (value != nullptr)
		value2 = escape_dup(*widget_pool, html_escape_class, value);

	if (result != NULL) {
		result = escape_dup(*widget_pool, html_escape_class, result);
	}

	SessionId session_id;
	session_id.Clear();

	FailingResourceLoader resource_loader;

	auto ctx = SharedPoolPtr<WidgetContext>::Make
		(pool, event_loop,
		 resource_loader, resource_loader,
		 nullptr,
		 site_name, nullptr,
		 nullptr, nullptr,
		 nullptr, nullptr,
		 "/index.html"sv,
		 nullptr,
		 nullptr, nullptr, session_id, "foo",
		 nullptr);

	auto istream = rewrite_widget_uri(*pool, std::move(ctx), nullptr,
					  *widget,
					  value2,
					  mode, stateful, view, &html_escape_class);
	if (result == NULL)
		ASSERT_FALSE(istream);
	else
		assert_istream_equals(pool, std::move(istream), result);
}

static void
assert_rewrite_check3(EventLoop &event_loop,
		      struct pool *widget_pool, Widget *widget,
		      const char *value, RewriteUriMode mode, bool stateful,
		      const char *view,
		      const char *result)
{
	assert_rewrite_check4(event_loop, widget_pool, nullptr, widget,
			      value, mode, stateful, view, result);
}

static void
assert_rewrite_check2(EventLoop &event_loop,
		      struct pool *widget_pool, Widget *widget,
		      const char *value, RewriteUriMode mode, bool stateful,
		      const char *result)
{
	assert_rewrite_check3(event_loop, widget_pool, widget,
			      value, mode, stateful, nullptr,
			      result);
}

static void
assert_rewrite_check(EventLoop &event_loop,
		     struct pool *widget_pool, Widget *widget,
		     const char *value, RewriteUriMode mode,
		     const char *result)
{
	assert_rewrite_check2(event_loop, widget_pool, widget,
			      value, mode, true, result);
}


/*
 * the main test code
 *
 */

TEST(RewriteUriTest, Basic)
{
	PInstance instance;
	auto &event_loop = instance.event_loop;

	const auto pool = pool_new_libc(instance.root_pool, "pool");

	/* set up input objects */

	Widget container(Widget::RootTag(), *pool, "foobar");

	/* test all modes with a normal widget */

	{
		Widget widget(*pool, nullptr);
		widget.class_name = "1";
		widget.parent = &container;
		widget.SetId("1");

		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
				     "http://widget-server/1/123");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=123");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::PARTIAL,
				     "/index.html;focus=1&path=123&frame=1");

		/* with query string */

		assert_rewrite_check(event_loop, pool, &widget,
				     "123?user=root&password=hansilein",
				     RewriteUriMode::DIRECT,
				     "http://widget-server/1/123?user=root&password=hansilein");

		assert_rewrite_check(event_loop, pool, &widget,
				     "123?user=root&password=hansilein",
				     RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=123?user=root&password=hansilein");

		assert_rewrite_check(event_loop, pool, &widget,
				     "123?user=root&password=hansilein",
				     RewriteUriMode::PARTIAL,
				     "/index.html;focus=1&path=123&frame=1"
				     "?user=root&password=hansilein");

		/* with NULL value */

		assert_rewrite_check(event_loop, pool, &widget, nullptr, RewriteUriMode::DIRECT,
				     "http://widget-server/1/");
		assert_rewrite_check(event_loop, pool, &widget, nullptr, RewriteUriMode::FOCUS,
				     "/index.html;focus=1");

		/* with empty value */

		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::DIRECT,
				     "http://widget-server/1/");
		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=");

		/* with configured path_info */

		widget.ClearLazy();
		widget.from_template.path_info = "456/";

		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/");
		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::FOCUS,
				     "/index.html;focus=1");

		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/123");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=456$2f123");

		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/");
		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=456$2f");

		/* with configured query string */

		widget.ClearLazy();
		widget.from_template.query_string = "a=b";

		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/?a=b");
		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::FOCUS,
				     "/index.html;focus=1");

		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/123?a=b");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=456$2f123");

		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/?a=b");
		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=456$2f");

		/* with both configured and supplied query string */

		assert_rewrite_check(event_loop, pool, &widget, "?c=d", RewriteUriMode::DIRECT,
				     "http://widget-server/1/456/?a=b&c=d");
		assert_rewrite_check(event_loop, pool, &widget, "?c=d", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=456$2f?c=d");

		/* session data */

		widget.ClearLazy();
		widget.from_template.query_string = "a=b";
		widget.from_request.path_info = "789/";
		widget.from_request.query_string = "e=f";

		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::DIRECT,
				     "http://widget-server/1/789/?a=b&e=f");
		assert_rewrite_check(event_loop, pool, &widget, NULL, RewriteUriMode::FOCUS,
				     "/index.html;focus=1");

		/*
		  assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
		  "http://widget-server/1/789/123?a=b");
		*/
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=789$2f123");

		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::DIRECT,
				     "http://widget-server/1/789/?a=b&e=f");
		assert_rewrite_check(event_loop, pool, &widget, "", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=789$2f?e=f");

		/* session data, but stateless */

		widget.ClearLazy();

		assert_rewrite_check2(event_loop, pool, &widget,
				      nullptr, RewriteUriMode::DIRECT, false,
				      "http://widget-server/1/456/?a=b");
		assert_rewrite_check2(event_loop, pool, &widget,
				      nullptr, RewriteUriMode::FOCUS, false,
				      "/index.html;focus=1");

		assert_rewrite_check2(event_loop, pool, &widget,
				      "123", RewriteUriMode::DIRECT, false,
				      "http://widget-server/1/456/123?a=b");
		assert_rewrite_check2(event_loop, pool, &widget,
				      "123", RewriteUriMode::FOCUS, false,
				      "/index.html;focus=1&path=456$2f123");

		assert_rewrite_check2(event_loop, pool, &widget,
				      "", RewriteUriMode::DIRECT, false,
				      "http://widget-server/1/456/?a=b");
		assert_rewrite_check2(event_loop, pool, &widget,
				      "", RewriteUriMode::FOCUS, false,
				      "/index.html;focus=1&path=456$2f");
	}

	/* without trailing slash in server URI; first with an invalid
	   suffix, which does not match the server URI */

	{
		Widget widget(*pool, nullptr);
		widget.class_name = "2";
		widget.parent = &container;
		widget.SetId("1");

		assert_rewrite_check(event_loop, pool, &widget, "@/foo", RewriteUriMode::DIRECT,
				     "http://widget-server/@/foo");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
				     "http://widget-server/123");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     NULL);
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::PARTIAL,
				     NULL);

		/* valid path */

		assert_rewrite_check(event_loop, pool, &widget, "2", RewriteUriMode::DIRECT,
				     "http://widget-server/2");
		assert_rewrite_check(event_loop, pool, &widget, "2", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=");

		/* valid path with path_info */

		assert_rewrite_check(event_loop, pool, &widget, "2/foo", RewriteUriMode::DIRECT,
				     "http://widget-server/2/foo");

		assert_rewrite_check(event_loop, pool, &widget, "2/foo", RewriteUriMode::FOCUS,
				     "/index.html;focus=1&path=$2ffoo");

		/* with view value */

		assert_rewrite_check3(event_loop, pool, &widget,
				      nullptr, RewriteUriMode::DIRECT, false, "foo",
				      "http://widget-server/2");
		assert_rewrite_check3(event_loop, pool, &widget,
				      nullptr, RewriteUriMode::FOCUS, false, "foo",
				      "/index.html;focus=1&view=foo");
	}

	/* test the "@/" syntax */

	{
		Widget widget(*pool, nullptr);
		widget.class_name = "3";
		widget.parent = &container;
		widget.SetId("id3");

		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::DIRECT,
				     "http://widget-server/123");
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::FOCUS,
				     NULL);
		assert_rewrite_check(event_loop, pool, &widget, "123", RewriteUriMode::PARTIAL,
				     NULL);
		assert_rewrite_check(event_loop, pool, &widget, "@/foo", RewriteUriMode::DIRECT,
				     "/resources/3/foo");
		assert_rewrite_check(event_loop, pool, &widget, "@/foo", RewriteUriMode::FOCUS,
				     "/resources/3/foo");
		assert_rewrite_check(event_loop, pool, &widget, "@/foo", RewriteUriMode::PARTIAL,
				     "/resources/3/foo");

		/* test RewriteUriMode::RESPONSE */

		assert_rewrite_check(event_loop, pool, &widget,
				     "123", RewriteUriMode::RESPONSE, "3");
	}

	/* test TRANSLATE_UNTRUSTED */

	{
		Widget widget(*pool, nullptr);
		widget.class_name = "untrusted_host";
		widget.parent = &container;
		widget.SetId("uh_id");

		assert_rewrite_check4(event_loop, pool, "mysite", &widget,
				      "123", RewriteUriMode::FOCUS, false,
				      nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");

		assert_rewrite_check4(event_loop, pool, "mysite", &widget,
				      "/1/123", RewriteUriMode::FOCUS, false,
				      nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");
	}

	/* test TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX */

	{
		Widget widget(*pool, nullptr);
		widget.class_name = "untrusted_raw_site_suffix";
		widget.parent = &container;
		widget.SetId("urss_id");

		assert_rewrite_check4(event_loop, pool, "mysite", &widget,
				      "123", RewriteUriMode::FOCUS, false,
				      nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");

		assert_rewrite_check4(event_loop, pool, "mysite", &widget,
				      "/1/123", RewriteUriMode::FOCUS, false,
				      nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");
	}
}
