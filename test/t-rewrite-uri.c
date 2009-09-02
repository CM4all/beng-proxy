#include "rewrite-uri.h"
#include "session.h"
#include "widget.h"
#include "widget-resolver.h"
#include "uri-parser.h"
#include "sink-gstring.h"
#include "tpool.h"
#include "async.h"

#include <glib.h>

/*
 * dummy implementations to satisfy the linker
 *
 */

const struct widget_class root_widget_class = {
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
    },
    .stateful = false,
};

struct session *
session_get(G_GNUC_UNUSED session_id_t id)
{
    return NULL;
}

void
session_put(G_GNUC_UNUSED struct session *session)
{
}

void
widget_sync_session(G_GNUC_UNUSED struct widget *widget,
                    G_GNUC_UNUSED struct session *session)
{
}


/*
 * A dummy resolver
 *
 */

void
widget_resolver_new(G_GNUC_UNUSED pool_t pool, G_GNUC_UNUSED pool_t widget_pool,
                    struct widget *widget,
                    G_GNUC_UNUSED struct tcache *translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
    static struct uri_with_address address1 = {
        .uri = "http://widget-server/1/",
    };
    static const struct widget_class class1 = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &address1,
            },
        },
    };
    static struct uri_with_address address2 = {
        .uri = "http://widget-server/2",
    };
    static const struct widget_class class2 = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &address2,
            },
        },
    };

    if (strcmp(widget->class_name, "1") == 0) {
        list_init(&address1.addresses);
        widget->class = &class1;
    } else if (strcmp(widget->class_name, "2") == 0) {
        list_init(&address2.addresses);
        widget->class = &class2;
    }

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
sink_gstring_callback(GString *value, void *_ctx)
{
    struct sink_gstring_ctx *ctx = _ctx;

    ctx->value = value;
    ctx->finished = true;
}

static void
assert_istream_equals(pool_t pool, istream_t istream, const char *value)
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
assert_rewrite_check2(pool_t widget_pool, struct widget *widget,
                      const char *value, enum uri_mode mode, bool stateful,
                      const char *result)
{
    pool_t pool = pool_new_libc(widget_pool, "rewrite");
    struct strref value2;
    istream_t istream;

    if (value != NULL)
        strref_set_c(&value2, value);

    istream = rewrite_widget_uri(pool, widget_pool, (struct tcache *)0x1,
                                 "cm4all.com", &external_uri,
                                 NULL, widget, 1,
                                 value != NULL ? &value2 : NULL,
                                 mode, stateful);
    if (result == NULL)
        assert(istream == NULL);
    else
        assert_istream_equals(pool, istream, result);

    pool_unref(pool);
}

static void
assert_rewrite_check(pool_t widget_pool, struct widget *widget,
                     const char *value, enum uri_mode mode,
                     const char *result)
{
    assert_rewrite_check2(widget_pool, widget, value, mode, true, result);
}


/*
 * the main test code
 *
 */

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv)
{
    int ret;
    pool_t root_pool, pool;
    struct widget container, widget;
    struct strref value;

    root_pool = pool_new_libc(NULL, "root");
    tpool_init(root_pool);

    pool = pool_new_libc(root_pool, "pool");

    /* set up input objects */

    widget_init(&container, pool, &root_widget_class);
    container.id = "foobar";
    container.lazy.path = "";
    container.lazy.prefix = "__";

    ret = uri_parse(pool, &external_uri, "/index.html;x=y?foo=bar");
    assert(ret == 0);

    /* test all modes with a normal widget */

    widget_init(&widget, pool, NULL);
    widget.class_name = "1";
    widget.parent = &container;
    strref_set_c(&value, "1");
    widget_set_id(&widget, pool, &value);

    strref_set_c(&value, "123");

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTIAL,
                         "/index.html;focus=1&path=123&frame=1");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTITION,
                         "http://__1__.cm4all.com/index.html;focus=1&path=123&frame=1");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PROXY,
                         "/index.html;focus=1&path=123&frame=1&raw=1");

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

    assert_rewrite_check(pool, &widget, "123?user=root&password=hansilein",
                         URI_MODE_PARTITION,
                         "http://__1__.cm4all.com/index.html;focus=1&path=123&frame=1"
                         "?user=root&password=hansilein");

    assert_rewrite_check(pool, &widget, "123?user=root&password=hansilein",
                         URI_MODE_PROXY,
                         "/index.html;focus=1&path=123&frame=1&raw=1"
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
                         "/index.html;focus=1&path=456%2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/456/");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456%2f");

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
                         "/index.html;focus=1&path=456%2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/456/?a=b");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456%2f");

    /* with both configured and supplied query string */

    assert_rewrite_check(pool, &widget, "?c=d", URI_MODE_DIRECT,
                         "http://widget-server/1/456/?a=b&c=d");
    assert_rewrite_check(pool, &widget, "?c=d", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=456%2f?c=d");

    /* session data */

    widget.lazy.address = NULL;
    widget.lazy.stateless_address = NULL;
    widget.query_string = "a=b";
    widget.from_request.path_info = "789/";
    strref_set_c(&widget.from_request.query_string, "e=f");

    assert_rewrite_check(pool, &widget, NULL, URI_MODE_DIRECT,
                         "http://widget-server/1/789/?a=b&e=f");
    assert_rewrite_check(pool, &widget, NULL, URI_MODE_FOCUS,
                         "/index.html;focus=1");

    /*
    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/1/789/123?a=b");
    */
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=789%2f123");

    assert_rewrite_check(pool, &widget, "", URI_MODE_DIRECT,
                         "http://widget-server/1/789/?a=b&e=f");
    assert_rewrite_check(pool, &widget, "", URI_MODE_FOCUS,
                         "/index.html;focus=1&path=789%2f?e=f");

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
                          "/index.html;focus=1&path=456%2f123");

    assert_rewrite_check2(pool, &widget, "", URI_MODE_DIRECT, false,
                          "http://widget-server/1/456/?a=b");
    assert_rewrite_check2(pool, &widget, "", URI_MODE_FOCUS, false,
                          "/index.html;focus=1&path=456%2f");

    /* without trailing slash in server URI; first with an invalid
       suffix, which does not match the server URI */

    widget_init(&widget, pool, NULL);
    widget.class_name = "2";
    widget.parent = &container;
    strref_set_c(&value, "1");
    widget_set_id(&widget, pool, &value);

    assert_rewrite_check(pool, &widget, "123", URI_MODE_DIRECT,
                         "http://widget-server/123");
    assert_rewrite_check(pool, &widget, "123", URI_MODE_FOCUS,
                         NULL);
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTIAL,
                         NULL);
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PARTITION,
                         NULL);
    assert_rewrite_check(pool, &widget, "123", URI_MODE_PROXY,
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
                         "/index.html;focus=1&path=%2ffoo");

    /* cleanup */

    pool_unref(pool);
    pool_unref(root_pool);
    tpool_deinit();
    pool_commit();
    pool_recycler_clear();
}
