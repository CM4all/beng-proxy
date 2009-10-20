#include "embed.h"
#include "uri-parser.h"
#include "widget.h"
#include "widget-http.h"
#include "widget-resolver.h"
#include "processor.h"
#include "async.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct session *
session_get(session_id_t id __attr_unused)
{
    return NULL;
}

void
session_put(struct session *session __attr_unused)
{
}

void
widget_sync_session(struct widget *widget __attr_unused,
                    struct session *session __attr_unused)
{
}

void
widget_http_request(pool_t pool __attr_unused, struct widget *widget __attr_unused,
                    struct processor_env *env __attr_unused,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref __attr_unused)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

struct test_operation {
    struct async_operation operation;
    pool_t pool;
};

static void
test_abort(struct async_operation *ao __attr_unused)
{
    struct test_operation *to = (struct test_operation *)ao;

    pool_unref(to->pool);
}

static const struct async_operation_class test_operation = {
    .abort = test_abort,
};

void
widget_resolver_new(pool_t pool, pool_t widget_pool __attr_unused,
                    struct widget *widget __attr_unused,
                    struct tcache *translate_cache __attr_unused,
                    widget_resolver_callback_t callback __attr_unused, void *ctx __attr_unused,
                    struct async_operation_ref *async_ref)
{
    struct test_operation *to = p_malloc(pool, sizeof(*to));

    to->pool = pool;

    async_init(&to->operation, &test_operation);
    async_ref_set(async_ref, &to->operation);
    pool_ref(pool);
}

static void
test_abort_resolver(pool_t pool)
{
    const char *uri;
    bool ret;
    struct parsed_uri parsed_uri;
    struct widget widget;
    struct processor_env env;
    istream_t istream;

    pool = pool_new_linear(pool, "test", 4096);

    uri = "/beng.html";
    ret = uri_parse(&parsed_uri, uri);
    if (!ret) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

    widget_init(&widget, pool, NULL);

    istream = embed_inline_widget(pool, &env, &widget);
    pool_unref(pool);

    istream_close(istream);
}

int main(int argc, char **argv) {
    pool_t pool;

    (void)argc;
    (void)argv;

    pool = pool_new_libc(NULL, "root");

    test_abort_resolver(pool);
    pool_commit();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();
}
