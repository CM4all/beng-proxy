#include "processor.h"
#include "penv.h"
#include "uri-parser.h"
#include "inline-widget.h"
#include "widget.h"
#include "widget-class.h"
#include "widget-lookup.h"
#include "rewrite-uri.h"
#include "async.h"
#include "istream.h"

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

struct istream *
embed_inline_widget(struct pool *pool,
                    gcc_unused struct processor_env *env,
                    struct widget *widget)
{
    const char *s = widget_path(widget);
    if (s == NULL)
        s = "widget";

    return istream_string_new(pool, s);
}

struct widget_session *
widget_get_session(gcc_unused struct widget *widget,
                   gcc_unused struct session *session,
                   gcc_unused bool create)
{
    return NULL;
}

enum uri_mode
parse_uri_mode(G_GNUC_UNUSED const struct strref *s)
{
    return URI_MODE_DIRECT;
}

struct istream *
rewrite_widget_uri(gcc_unused struct pool *pool, gcc_unused struct pool *widget_pool,
                   gcc_unused struct tcache *translate_cache,
                   gcc_unused const char *absolute_uri,
                   gcc_unused const struct parsed_uri *external_uri,
                   gcc_unused const char *site_name,
                   gcc_unused const char *untrusted_host,
                   gcc_unused struct strmap *args,
                   gcc_unused struct widget *widget,
                   gcc_unused session_id_t session_id,
                   gcc_unused const struct strref *value,
                   gcc_unused enum uri_mode mode,
                   gcc_unused bool stateful,
                   gcc_unused const char *view,
                   gcc_unused const struct escape_class *escape)
{
    return NULL;
}

/*
 * http_response_handler
 *
 */

static void
my_widget_found(gcc_unused struct widget *widget, gcc_unused void *ctx)
{
    g_printerr("widget found\n");
}

static void
my_widget_not_found(gcc_unused void *ctx)
{
    g_printerr("widget not found\n");
}

static void
my_widget_error(GError *error, gcc_unused void *ctx)
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
test_proxy_abort(struct pool *pool)
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
    struct pool *pool;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    test_proxy_abort(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
