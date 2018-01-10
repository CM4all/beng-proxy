/*
 * Copyright 2007-2017 Content Management AG
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

#include "FailingResourceLoader.hxx"
#include "tconstruct.hxx"
#include "widget/RewriteUri.hxx"
#include "http_address.hxx"
#include "session.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Resolver.hxx"
#include "uri/Dissect.hxx"
#include "PInstance.hxx"
#include "escape_pool.hxx"
#include "escape_html.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "istream/StringSink.hxx"
#include "penv.hxx"
#include "widget/Inline.hxx"
#include "util/Cancellable.hxx"

#include <gtest/gtest.h>

const struct timeval inline_widget_body_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

struct MakeWidgetClass : WidgetClass {
    explicit MakeWidgetClass(struct pool &p, const char *uri) {
        Init();

        auto http = MakeHttpAddress(uri).Host("widget-server");
        views.address = *NewFromPool<HttpAddress>(p, p, http);
    }
};


/*
 * dummy implementations to satisfy the linker
 *
 */

RealmSession *
Session::GetRealm(const char *)
{
    return nullptr;
}

Session *
session_get(gcc_unused SessionId id)
{
    return NULL;
}

void
session_put(gcc_unused Session *session)
{
}

void
Widget::LoadFromSession(gcc_unused RealmSession &session)
{
}

UnusedIstreamPtr
embed_inline_widget(struct pool &pool, gcc_unused struct processor_env &env,
                    gcc_unused bool plain_text,
                    Widget &widget)
{
    return UnusedIstreamPtr(istream_string_new(&pool, widget.class_name));
}

/*
 * A dummy resolver
 *
 */

void
ResolveWidget(gcc_unused struct pool &pool,
              Widget &widget,
              gcc_unused struct tcache &translate_cache,
              WidgetResolverCallback callback,
              gcc_unused CancellablePointer &cancel_ptr)
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
            &widget.cls->views;

    callback();
}


/*
 * Check utilities
 *
 */

static DissectedUri external_uri;

struct StringSinkCtx {
    std::string value;
    bool finished = false;
};

static void
sink_gstring_callback(std::string &&value, std::exception_ptr, void *_ctx)
{
    auto &ctx = *(StringSinkCtx *)_ctx;

    ctx.value = std::move(value);
    ctx.finished = true;
}

static void
assert_istream_equals(struct pool *pool, UnusedIstreamPtr _istream,
                      const char *value)
{
    StringSinkCtx ctx;
    CancellablePointer cancel_ptr;

    ASSERT_TRUE(_istream);
    ASSERT_NE(value, nullptr);

    Istream *istream = _istream.Steal();

    NewStringSink(*pool, *istream, sink_gstring_callback, &ctx, cancel_ptr);

    while (!ctx.finished)
        istream->Read();

    ASSERT_STREQ(ctx.value.c_str(), value);
}

static void
assert_rewrite_check4(EventLoop &event_loop,
                      struct pool *widget_pool, const char *site_name,
                      Widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *view,
                      const char *result)
{
    struct pool *pool = pool_new_libc(widget_pool, "rewrite");

    StringView value2 = value;
    if (!value2.IsNull())
        value2 = escape_dup(widget_pool, &html_escape_class, value2);

    if (result != NULL) {
        result = escape_dup(widget_pool, &html_escape_class, result);
    }

    SessionId session_id;
    session_id.Clear();

    FailingResourceLoader resource_loader;
    struct processor_env env(widget_pool, event_loop,
                             resource_loader, resource_loader,
                             site_name, nullptr,
                             nullptr, nullptr,
                             nullptr, nullptr,
                             &external_uri,
                             nullptr,
                             nullptr, session_id, "foo",
                             nullptr);

    auto istream = rewrite_widget_uri(*pool, env, *(struct tcache *)0x1,
                                      *widget,
                                      value2,
                                      mode, stateful, view, &html_escape_class);
    if (result == NULL)
        ASSERT_FALSE(istream);
    else
        assert_istream_equals(pool, std::move(istream), result);

    pool_unref(pool);
}

static void
assert_rewrite_check3(EventLoop &event_loop,
                      struct pool *widget_pool, Widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *view,
                      const char *result)
{
    assert_rewrite_check4(event_loop, widget_pool, nullptr, widget,
                          value, mode, stateful, view, result);
}

static void
assert_rewrite_check2(EventLoop &event_loop,
                      struct pool *widget_pool, Widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *result)
{
    assert_rewrite_check3(event_loop, widget_pool, widget,
                          value, mode, stateful, nullptr,
                          result);
}

static void
assert_rewrite_check(EventLoop &event_loop,
                     struct pool *widget_pool, Widget *widget,
                     const char *value, enum uri_mode mode,
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

    auto *pool = pool_new_libc(instance.root_pool, "pool");

    /* set up input objects */

    Widget container(Widget::RootTag(), *pool, "foobar");

    ASSERT_TRUE(external_uri.Parse("/index.html;x=y?foo=bar"));

    /* test all modes with a normal widget */

    {
        Widget widget(*pool, nullptr);
        widget.class_name = "1";
        widget.parent = &container;
        widget.SetId("1");

        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
                             "http://widget-server/1/123");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=123");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_PARTIAL,
                             "/index.html;focus=1&path=123&frame=1");

        /* with query string */

        assert_rewrite_check(event_loop, pool, &widget,
                             "123?user=root&password=hansilein",
                             URI_MODE_DIRECT,
                             "http://widget-server/1/123?user=root&password=hansilein");

        assert_rewrite_check(event_loop, pool, &widget,
                             "123?user=root&password=hansilein",
                             URI_MODE_FOCUS,
                             "/index.html;focus=1&path=123?user=root&password=hansilein");

        assert_rewrite_check(event_loop, pool, &widget,
                             "123?user=root&password=hansilein",
                             URI_MODE_PARTIAL,
                             "/index.html;focus=1&path=123&frame=1"
                             "?user=root&password=hansilein");

        /* with NULL value */

        assert_rewrite_check(event_loop, pool, &widget, nullptr, URI_MODE_DIRECT,
                             "http://widget-server/1/");
        assert_rewrite_check(event_loop, pool, &widget, nullptr, URI_MODE_FOCUS,
                             "/index.html;focus=1");

        /* with empty value */

        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_DIRECT,
                             "http://widget-server/1/");
        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=");

        /* with configured path_info */

        widget.ClearLazy();
        widget.from_template.path_info = "456/";

        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_DIRECT,
                             "http://widget-server/1/456/");
        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_FOCUS,
                             "/index.html;focus=1");

        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
                             "http://widget-server/1/456/123");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=456$2f123");

        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_DIRECT,
                             "http://widget-server/1/456/");
        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=456$2f");

        /* with configured query string */

        widget.ClearLazy();
        widget.from_template.query_string = "a=b";

        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_DIRECT,
                             "http://widget-server/1/456/?a=b");
        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_FOCUS,
                             "/index.html;focus=1");

        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
                             "http://widget-server/1/456/123?a=b");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=456$2f123");

        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_DIRECT,
                             "http://widget-server/1/456/?a=b");
        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=456$2f");

        /* with both configured and supplied query string */

        assert_rewrite_check(event_loop, pool, &widget, "?c=d", URI_MODE_DIRECT,
                             "http://widget-server/1/456/?a=b&c=d");
        assert_rewrite_check(event_loop, pool, &widget, "?c=d", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=456$2f?c=d");

        /* session data */

        widget.ClearLazy();
        widget.from_template.query_string = "a=b";
        widget.from_request.path_info = "789/";
        widget.from_request.query_string = "e=f";

        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_DIRECT,
                             "http://widget-server/1/789/?a=b&e=f");
        assert_rewrite_check(event_loop, pool, &widget, NULL, URI_MODE_FOCUS,
                             "/index.html;focus=1");

        /*
          assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
          "http://widget-server/1/789/123?a=b");
        */
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=789$2f123");

        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_DIRECT,
                             "http://widget-server/1/789/?a=b&e=f");
        assert_rewrite_check(event_loop, pool, &widget, "", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=789$2f?e=f");

        /* session data, but stateless */

        widget.ClearLazy();

        assert_rewrite_check2(event_loop, pool, &widget,
                              nullptr, URI_MODE_DIRECT, false,
                              "http://widget-server/1/456/?a=b");
        assert_rewrite_check2(event_loop, pool, &widget,
                              nullptr, URI_MODE_FOCUS, false,
                              "/index.html;focus=1");

        assert_rewrite_check2(event_loop, pool, &widget,
                              "123", URI_MODE_DIRECT, false,
                              "http://widget-server/1/456/123?a=b");
        assert_rewrite_check2(event_loop, pool, &widget,
                              "123", URI_MODE_FOCUS, false,
                              "/index.html;focus=1&path=456$2f123");

        assert_rewrite_check2(event_loop, pool, &widget,
                              "", URI_MODE_DIRECT, false,
                              "http://widget-server/1/456/?a=b");
        assert_rewrite_check2(event_loop, pool, &widget,
                              "", URI_MODE_FOCUS, false,
                              "/index.html;focus=1&path=456$2f");
    }

    /* without trailing slash in server URI; first with an invalid
       suffix, which does not match the server URI */

    {
        Widget widget(*pool, nullptr);
        widget.class_name = "2";
        widget.parent = &container;
        widget.SetId("1");

        assert_rewrite_check(event_loop, pool, &widget, "@/foo", URI_MODE_DIRECT,
                             "http://widget-server/@/foo");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
                             "http://widget-server/123");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             NULL);
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_PARTIAL,
                             NULL);

        /* valid path */

        assert_rewrite_check(event_loop, pool, &widget, "2", URI_MODE_DIRECT,
                             "http://widget-server/2");
        assert_rewrite_check(event_loop, pool, &widget, "2", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=");

        /* valid path with path_info */

        assert_rewrite_check(event_loop, pool, &widget, "2/foo", URI_MODE_DIRECT,
                             "http://widget-server/2/foo");

        assert_rewrite_check(event_loop, pool, &widget, "2/foo", URI_MODE_FOCUS,
                             "/index.html;focus=1&path=$2ffoo");

        /* with view value */

        assert_rewrite_check3(event_loop, pool, &widget,
                              nullptr, URI_MODE_DIRECT, false, "foo",
                              "http://widget-server/2");
        assert_rewrite_check3(event_loop, pool, &widget,
                              nullptr, URI_MODE_FOCUS, false, "foo",
                              "/index.html;focus=1&view=foo");
    }

    /* test the "@/" syntax */

    {
        Widget widget(*pool, nullptr);
        widget.class_name = "3";
        widget.parent = &container;
        widget.SetId("id3");

        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_DIRECT,
                             "http://widget-server/123");
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_FOCUS,
                             NULL);
        assert_rewrite_check(event_loop, pool, &widget, "123", URI_MODE_PARTIAL,
                             NULL);
        assert_rewrite_check(event_loop, pool, &widget, "@/foo", URI_MODE_DIRECT,
                             "/resources/3/foo");
        assert_rewrite_check(event_loop, pool, &widget, "@/foo", URI_MODE_FOCUS,
                             "/resources/3/foo");
        assert_rewrite_check(event_loop, pool, &widget, "@/foo", URI_MODE_PARTIAL,
                             "/resources/3/foo");

        /* test URI_MODE_RESPONSE */

        assert_rewrite_check(event_loop, pool, &widget,
                             "123", URI_MODE_RESPONSE, "3");
    }

    /* test TRANSLATE_UNTRUSTED */

    {
        Widget widget(*pool, nullptr);
        widget.class_name = "untrusted_host";
        widget.parent = &container;
        widget.SetId("uh_id");

        assert_rewrite_check4(event_loop, pool, "mysite", &widget,
                              "123", URI_MODE_FOCUS, false,
                              nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");

        assert_rewrite_check4(event_loop, pool, "mysite", &widget,
                              "/1/123", URI_MODE_FOCUS, false,
                              nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");
    }

    /* test TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX */

    {
        Widget widget(*pool, nullptr);
        widget.class_name = "untrusted_raw_site_suffix";
        widget.parent = &container;
        widget.SetId("urss_id");

        assert_rewrite_check4(event_loop, pool, "mysite", &widget,
                              "123", URI_MODE_FOCUS, false,
                              nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");

        assert_rewrite_check4(event_loop, pool, "mysite", &widget,
                              "/1/123", URI_MODE_FOCUS, false,
                              nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");
    }

    /* cleanup */

    pool_unref(pool);
}
