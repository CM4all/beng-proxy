#include "tconstruct.hxx"
#include "rewrite_uri.hxx"
#include "http_address.hxx"
#include "session.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_resolver.hxx"
#include "widget_request.hxx"
#include "uri/uri_parser.hxx"
#include "tpool.hxx"
#include "async.hxx"
#include "escape_pool.hxx"
#include "escape_html.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "istream/sink_gstring.hxx"
#include "penv.hxx"
#include "inline_widget.hxx"

#include <glib.h>

struct MakeWidgetClass : WidgetClass {
    explicit MakeWidgetClass(struct pool &p, const char *uri) {
        Init();

        auto http = MakeHttpAddress(uri).Host("widget-server");
        views.address = ResourceAddress(ResourceAddress::Type::HTTP,
                                        *http_address_dup(p, &http));
    }
};


/*
 * dummy implementations to satisfy the linker
 *
 */

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
widget_sync_session(gcc_unused struct widget *widget,
                    gcc_unused Session *session)
{
}

struct istream *
embed_inline_widget(struct pool &pool, gcc_unused struct processor_env &env,
                    gcc_unused bool plain_text,
                    struct widget &widget)
{
    return istream_string_new(&pool, widget.class_name);
}

/*
 * A dummy resolver
 *
 */

void
widget_resolver_new(gcc_unused struct pool &pool,
                    gcc_unused struct pool &widget_pool,
                    struct widget &widget,
                    gcc_unused struct tcache &translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    gcc_unused struct async_operation_ref &async_ref)
{

    if (strcmp(widget.class_name, "1") == 0) {
        widget.cls = NewFromPool<MakeWidgetClass>(widget_pool, widget_pool, "/1/");
    } else if (strcmp(widget.class_name, "2") == 0) {
        widget.cls = NewFromPool<MakeWidgetClass>(widget_pool, widget_pool, "/2");
    } else if (strcmp(widget.class_name, "3") == 0) {
        auto *cls = NewFromPool<MakeWidgetClass>(widget_pool, widget_pool, "/3");
        cls->local_uri = "/resources/3/";
        widget.cls = cls;
    } else if (strcmp(widget.class_name, "untrusted_host") == 0) {
        auto *cls = NewFromPool<MakeWidgetClass>(widget_pool, widget_pool, "/1/");
        cls->untrusted_host = "untrusted.host";
        widget.cls = cls;
    } else if (strcmp(widget.class_name, "untrusted_raw_site_suffix") == 0) {
        auto *cls = NewFromPool<MakeWidgetClass>(widget_pool, widget_pool, "/1/");
        cls->untrusted_raw_site_suffix = "_urss";
        widget.cls = cls;
    }

    if (widget.cls != NULL)
        widget.view = widget.from_request.view = &widget.cls->views;

    callback(ctx);
}


/*
 * Check utilities
 *
 */

static struct parsed_uri external_uri;

struct sink_gstring_ctx {
    GString *value;
    bool finished;
};

static void
sink_gstring_callback(GString *value, GError *error, void *_ctx)
{
    struct sink_gstring_ctx *ctx = (struct sink_gstring_ctx *)_ctx;

    if (error != NULL)
        g_error_free(error);

    ctx->value = value;
    ctx->finished = true;
}

static void
assert_istream_equals(struct pool *pool, struct istream *istream, const char *value)
{
    struct sink_gstring_ctx ctx = { .finished = false };
    struct async_operation_ref async_ref;

    assert(istream != NULL);
    assert(value != NULL);

    sink_gstring_new(pool, istream, sink_gstring_callback, &ctx, &async_ref);

    while (!ctx.finished)
        istream_read(istream);

    assert(ctx.value != NULL);
    /*g_print("value='%s'\n", sg->value->str);*/
    assert(strcmp(ctx.value->str, value) == 0);

    g_string_free(ctx.value, true);
}

static void
assert_rewrite_check4(struct pool *widget_pool, const char *site_name,
                      struct widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *view,
                      const char *result)
{
    struct pool *pool = pool_new_libc(widget_pool, "rewrite");
    struct istream *istream;

    StringView value2 = value;
    if (!value2.IsNull())
        value2 = escape_dup(widget_pool, &html_escape_class, value2);

    if (result != NULL) {
        result = escape_dup(widget_pool, &html_escape_class, result);
    }

    SessionId session_id;
    session_id.Clear();

    struct processor_env env(widget_pool,
                             site_name, nullptr,
                             nullptr, nullptr,
                             nullptr, nullptr,
                             &external_uri,
                             nullptr,
                             nullptr, session_id,
                             HTTP_METHOD_GET,
                             nullptr);

    istream = rewrite_widget_uri(*pool, *widget_pool, env, *(struct tcache *)0x1,
                                 *widget,
                                 value2,
                                 mode, stateful, view, &html_escape_class);
    if (result == NULL)
        assert(istream == NULL);
    else
        assert_istream_equals(pool, istream, result);

    pool_unref(pool);
}

static void
assert_rewrite_check3(struct pool *widget_pool, struct widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *view,
                      const char *result)
{
    assert_rewrite_check4(widget_pool, nullptr, widget,
                          value, mode, stateful, view, result);
}

static void
assert_rewrite_check2(struct pool *widget_pool, struct widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *result)
{
    assert_rewrite_check3(widget_pool, widget, value, mode, stateful, NULL,
                          result);
}

static void
assert_rewrite_check(struct pool *widget_pool, struct widget *widget,
                     const char *value, enum uri_mode mode,
                     const char *result)
{
    assert_rewrite_check2(widget_pool, widget, value, mode, true, result);
}


/*
 * the main test code
 *
 */

int main(gcc_unused int argc, gcc_unused char **argv)
{
    bool ret;
    struct pool *root_pool, *pool;
    struct widget container, widget;

    root_pool = pool_new_libc(NULL, "root");
    tpool_init(root_pool);

    pool = pool_new_libc(root_pool, "pool");

    /* set up input objects */

    container.Init(*pool, &root_widget_class);
    container.id = "foobar";
    container.lazy.path = "";
    container.lazy.prefix = "__";

    ret = external_uri.Parse("/index.html;x=y?foo=bar");
    assert(ret);

    /* test all modes with a normal widget */

    widget.Init(*pool, nullptr);
    widget.class_name = "1";
    widget.parent = &container;
    widget.SetId("1");

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTIAL,
                         "/index.html;focus=1&path=123&frame=1");

    /* with query string */

    assert_rewrite_check(pool, &widget, "123?user=root&password=hansilein",
                         URI_MODE_DIRECT,
                         "http://widget-server/1/123?user=root&password=hansilein");

    assert_rewrite_check(pool, &widget, "123?user=root&password=hansilein",
                         URI_MODE_FOCUS,
                         "/index.html;focus=1&path=123?user=root&password=hansilein");

    assert_rewrite_check(pool, &widget, "123?user=root&password=hansilein",
                         URI_MODE_PARTIAL,
                         "/index.html;focus=1&path=123&frame=1"
                         "?user=root&password=hansilein");

    /* with NULL value */

    assert_rewrite_check(pool, &widget, NULL, URI_MODE_DIRECT,
                         "http://widget-server/1/");
    assert_rewrite_check(pool, &widget, NULL, URI_MODE_FOCUS,
                         "/index.html;focus=1");

    /* with empty value */

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=");

    /* with configured path_info */

    widget.lazy.address = NULL;
    widget.lazy.stateless_address = NULL;
    widget.path_info = "456/";

    assert_rewrite_check(pool, &widget, NULL, URI_MODE_DIRECT,
                         "http://widget-server/1/456/");
    assert_rewrite_check(pool, &widget, NULL, URI_MODE_FOCUS,
                         "/index.html;focus=1");

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/456/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456$2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/456/");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456$2f");

    /* with configured query string */

    widget.lazy.address = NULL;
    widget.lazy.stateless_address = NULL;
    widget.query_string = "a=b";

    assert_rewrite_check(pool, &widget, NULL, URI_MODE_DIRECT,
                         "http://widget-server/1/456/?a=b");
    assert_rewrite_check(pool, &widget, NULL, URI_MODE_FOCUS,
                         "/index.html;focus=1");

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/456/123?a=b");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456$2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/456/?a=b");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456$2f");

    /* with both configured and supplied query string */

    assert_rewrite_check(pool, &widget, "?c=d", URI_MODE_DIRECT,
                         "http://widget-server/1/456/?a=b&c=d");
    assert_rewrite_check(pool, &widget, "?c=d", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456$2f?c=d");

    /* session data */

    widget.lazy.address = NULL;
    widget.lazy.stateless_address = NULL;
    widget.query_string = "a=b";
    widget.from_request.path_info = "789/";
    widget.from_request.query_string = "e=f";

    assert_rewrite_check(pool, &widget, NULL, URI_MODE_DIRECT,
                         "http://widget-server/1/789/?a=b&e=f");
    assert_rewrite_check(pool, &widget, NULL, URI_MODE_FOCUS,
                         "/index.html;focus=1");

    /*
    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/789/123?a=b");
    */
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=789$2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/789/?a=b&e=f");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=789$2f?e=f");

    /* session data, but stateless */

    widget.lazy.address = NULL;
    widget.lazy.stateless_address = NULL;

    assert_rewrite_check2(pool, &widget, NULL, URI_MODE_DIRECT, false,
                          "http://widget-server/1/456/?a=b");
    assert_rewrite_check2(pool, &widget, NULL, URI_MODE_FOCUS, false,
                          "/index.html;focus=1");

    assert_rewrite_check2(pool, &widget, "123", URI_MODE_DIRECT, false,
                          "http://widget-server/1/456/123?a=b");
    assert_rewrite_check2(pool, &widget, "123", URI_MODE_FOCUS, false,
                          "/index.html;focus=1&path=456$2f123");

    assert_rewrite_check2(pool, &widget, "", URI_MODE_DIRECT, false,
                          "http://widget-server/1/456/?a=b");
    assert_rewrite_check2(pool, &widget, "", URI_MODE_FOCUS, false,
                          "/index.html;focus=1&path=456$2f");

    /* without trailing slash in server URI; first with an invalid
       suffix, which does not match the server URI */

    widget.Init(*pool, nullptr);
    widget.class_name = "2";
    widget.parent = &container;
    widget.SetId("1");

    assert_rewrite_check(pool, &widget, "@/foo", URI_MODE_DIRECT,
                         "http://widget-server/@/foo");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         NULL);
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTIAL,
                         NULL);

    /* valid path */

    assert_rewrite_check(pool, &widget, "2", URI_MODE_DIRECT,
                         "http://widget-server/2");
    assert_rewrite_check(pool, &widget, "2", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=");

    /* valid path with path_info */

    assert_rewrite_check(pool, &widget, "2/foo", URI_MODE_DIRECT,
                         "http://widget-server/2/foo");

    assert_rewrite_check(pool, &widget, "2/foo", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=$2ffoo");

    /* with view value */

    assert_rewrite_check3(pool, &widget, NULL, URI_MODE_DIRECT, false, "foo",
                          "http://widget-server/2");
    assert_rewrite_check3(pool, &widget, NULL, URI_MODE_FOCUS, false, "foo",
                          "/index.html;focus=1&view=foo");

    /* test the "@/" syntax */

    widget.Init(*pool, nullptr);
    widget.class_name = "3";
    widget.parent = &container;
    widget.SetId("id3");

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         NULL);
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTIAL,
                         NULL);
    assert_rewrite_check(pool, &widget, "@/foo", URI_MODE_DIRECT,
                         "/resources/3/foo");
    assert_rewrite_check(pool, &widget, "@/foo", URI_MODE_FOCUS,
                         "/resources/3/foo");
    assert_rewrite_check(pool, &widget, "@/foo", URI_MODE_PARTIAL,
                         "/resources/3/foo");

    /* test URI_MODE_RESPONSE */

    assert_rewrite_check(pool, &widget, "123", URI_MODE_RESPONSE, "3");

    /* test TRANSLATE_UNTRUSTED */

    widget.Init(*pool, nullptr);
    widget.class_name = "untrusted_host";
    widget.parent = &container;
    widget.SetId("uh_id");

    assert_rewrite_check4(pool, "mysite", &widget,
                          "123", URI_MODE_FOCUS, false,
                          nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");

    assert_rewrite_check4(pool, "mysite", &widget,
                          "/1/123", URI_MODE_FOCUS, false,
                          nullptr, "//untrusted.host/index.html;focus=uh_id&path=123");

    /* test TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX */

    widget.Init(*pool, nullptr);
    widget.class_name = "untrusted_raw_site_suffix";
    widget.parent = &container;
    widget.SetId("urss_id");

    assert_rewrite_check4(pool, "mysite", &widget,
                          "123", URI_MODE_FOCUS, false,
                          nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");

    assert_rewrite_check4(pool, "mysite", &widget,
                          "/1/123", URI_MODE_FOCUS, false,
                          nullptr, "//mysite_urss/index.html;focus=urss_id&path=123");

    /* cleanup */

    pool_unref(pool);
    pool_unref(root_pool);
    tpool_deinit();
    pool_commit();
    pool_recycler_clear();
}
