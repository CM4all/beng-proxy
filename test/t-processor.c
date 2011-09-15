#include "processor.h"
#include "penv.h"
#include "uri-parser.h"
#include "inline-widget.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-lookup.h"
#include "rewrite-uri.h"
#include "async.h"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/*
 * emulate missing libraries
 *
 */

const struct widget_class root_widget_class = {
    .views = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
    },
    .stateful = false,
};

struct tcache *global_translate_cache;

istream_t
embed_inline_widget(pool_t pool,
                    __attr_unused struct processor_env *env,
                    struct widget *widget)
{
    const char *s = widget_path(widget);
    if (s == NULL)
        s = "widget";

    return istream_string_new(pool, s);
}

struct widget_session *
widget_get_session(__attr_unused struct widget *widget,
                   __attr_unused struct session *session,
                   __attr_unused bool create)
{
    return NULL;
}

enum uri_mode
parse_uri_mode(G_GNUC_UNUSED const struct strref *s)
{
    return URI_MODE_DIRECT;
}

istream_t
rewrite_widget_uri(__attr_unused pool_t pool, __attr_unused pool_t widget_pool,
                   __attr_unused struct tcache *translate_cache,
                   __attr_unused const char *absolute_uri,
                   __attr_unused const struct parsed_uri *external_uri,
                   __attr_unused const char *site_name,
                   __attr_unused const char *untrusted_host,
                   __attr_unused struct strmap *args,
                   __attr_unused struct widget *widget,
                   __attr_unused session_id_t session_id,
                   __attr_unused const struct strref *value,
                   __attr_unused enum uri_mode mode,
                   __attr_unused bool stateful,
                   __attr_unused const char *view,
                   __attr_unused const struct escape_class *escape)
{
    return NULL;
}

/*
 * http_response_handler
 *
 */

static void
my_widget_found(__attr_unused struct widget *widget, __attr_unused void *ctx)
{
    g_printerr("widget found\n");
}

static void
my_widget_not_found(__attr_unused void *ctx)
{
    g_printerr("widget not found\n");
}

static void
my_widget_error(GError *error, __attr_unused void *ctx)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static const struct widget_lookup_handler my_widget_lookup_handler = {
    .found = my_widget_found,
    .not_found = my_widget_not_found,
    .error = my_widget_error,
};

/*
 * tests
 *
 */

static void
test_proxy_abort(pool_t pool)
{
    bool success;

    pool = pool_new_libc(pool, "test");

    struct parsed_uri parsed_uri;
    const char *uri = "/beng.html";
    success = uri_parse(&parsed_uri, uri);
    assert(success);

    struct widget widget;
    widget_init(&widget, pool, &root_widget_class);

    struct processor_env env;
    processor_env_init(pool, &env,
                       NULL, NULL,
                       "localhost:8080",
                       "localhost:8080",
                       "/beng.html",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       0xdeadbeef,
                       HTTP_METHOD_GET, NULL,
                       NULL);

    struct async_operation_ref async_ref;
    processor_lookup_widget(pool, HTTP_STATUS_OK, istream_block_new(pool),
                            &widget, "foo", &env, PROCESSOR_CONTAINER,
                            &my_widget_lookup_handler, NULL,
                            &async_ref);

    pool_unref(pool);

    async_abort(&async_ref);

    pool_commit();
}

int main(int argc, char **argv) {
    pool_t pool;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    test_proxy_abort(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
