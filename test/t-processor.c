#include "processor.h"
#include "uri-parser.h"
#include "embed.h"
#include "widget.h"
#include "widget-stream.h"
#include "rewrite-uri.h"

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
    .address = {
        .type = RESOURCE_ADDRESS_NONE,
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

void
embed_frame_widget(__attr_unused pool_t pool,
                   __attr_unused struct processor_env *env,
                   __attr_unused struct widget *widget,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   __attr_unused struct async_operation_ref *async_ref)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

struct widget_session *
widget_get_session(__attr_unused struct widget *widget,
                   __attr_unused struct session *session,
                   __attr_unused bool create)
{
    return NULL;
}

istream_t
rewrite_widget_uri(__attr_unused pool_t pool, __attr_unused pool_t widget_pool,
                   __attr_unused struct tcache *translate_cache,
                   __attr_unused const char *absolute_uri,
                   __attr_unused const struct parsed_uri *external_uri,
                   __attr_unused const char *untrusted_host,
                   __attr_unused struct strmap *args,
                   __attr_unused struct widget *widget,
                   __attr_unused session_id_t session_id,
                   __attr_unused const struct strref *value,
                   __attr_unused enum uri_mode mode,
                   __attr_unused bool stateful)
{
    return NULL;
}

/*
 * http_response_handler
 *
 */

static void
my_response(G_GNUC_UNUSED http_status_t status,
            G_GNUC_UNUSED struct strmap *headers,
            G_GNUC_UNUSED istream_t body,
            G_GNUC_UNUSED void *ctx)
{
}

static void
my_response_abort(G_GNUC_UNUSED void *ctx)
{
}

static const struct http_response_handler my_response_handler = {
    .response = my_response,
    .abort = my_response_abort,
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
    struct widget_ref proxy_ref = { .next = NULL, .id = "foo" };
    widget.from_request.proxy_ref = &proxy_ref;

    struct processor_env env;
    processor_env_init(pool, &env,
                       NULL,
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
    processor_new(pool, HTTP_STATUS_OK, NULL,
                  istream_block_new(pool),
                  &widget, &env, PROCESSOR_CONTAINER,
                  &my_response_handler, NULL,
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
