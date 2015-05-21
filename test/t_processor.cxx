#include "processor.h"
#include "penv.hxx"
#include "uri_parser.hxx"
#include "inline_widget.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_lookup.hxx"
#include "rewrite_uri.hxx"
#include "async.hxx"
#include "istream.h"
#include "istream_block.hxx"
#include "istream_string.hxx"

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

const WidgetClass root_widget_class = {
    .views = {
        .address = {
            .type = RESOURCE_ADDRESS_NONE,
        },
    },
    .stateful = false,
    .container_groups = StringSet(),
};

struct tcache *global_translate_cache;

struct istream *
embed_inline_widget(struct pool &pool,
                    gcc_unused struct processor_env &env,
                    gcc_unused bool plain_text,
                    struct widget &widget)
{
    const char *s = widget.GetIdPath();
    if (s == nullptr)
        s = "widget";

    return istream_string_new(&pool, s);
}

WidgetSession *
widget_get_session(gcc_unused struct widget *widget,
                   gcc_unused Session *session,
                   gcc_unused bool create)
{
    return nullptr;
}

enum uri_mode
parse_uri_mode(gcc_unused const struct strref &s)
{
    return URI_MODE_DIRECT;
}

struct istream *
rewrite_widget_uri(gcc_unused struct pool &pool,
                   gcc_unused struct pool &widget_pool,
                   gcc_unused struct processor_env &env,
                   gcc_unused struct tcache &translate_cache,
                   gcc_unused struct widget &widget,
                   gcc_unused const struct strref *value,
                   gcc_unused enum uri_mode mode,
                   gcc_unused bool stateful,
                   gcc_unused const char *view,
                   gcc_unused const struct escape_class *escape)
{
    return nullptr;
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
    success = parsed_uri.Parse(uri);
    assert(success);

    struct widget widget;
    widget.Init(*pool, &root_widget_class);

    SessionId session_id;
    session_id.Generate();

    struct processor_env env(pool,
                             nullptr, nullptr,
                             "localhost:8080",
                             "localhost:8080",
                             "/beng.html",
                             "http://localhost:8080/beng.html",
                             &parsed_uri,
                             nullptr,
                             "bp_session", session_id,
                             HTTP_METHOD_GET, nullptr);

    struct async_operation_ref async_ref;
    processor_lookup_widget(pool, istream_block_new(*pool),
                            &widget, "foo", &env, PROCESSOR_CONTAINER,
                            &my_widget_lookup_handler, nullptr,
                            &async_ref);

    pool_unref(pool);

    async_ref.Abort();

    pool_commit();
}

int main(int argc, char **argv) {
    struct pool *pool;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(nullptr, "root");

    test_proxy_abort(pool);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
