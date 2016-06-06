#include "inline_widget.hxx"
#include "uri/uri_parser.hxx"
#include "widget.hxx"
#include "widget_http.hxx"
#include "widget_resolver.hxx"
#include "widget_request.hxx"
#include "processor.hxx"
#include "penv.hxx"
#include "async.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"
#include "istream/istream_iconv.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "session.hxx"
#include "event/Loop.hxx"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

const char *
widget::GetLogName() const
{
    return "dummy";
}

Istream *
istream_iconv_new(gcc_unused struct pool *pool, Istream &input,
                  gcc_unused const char *tocode,
                  gcc_unused const char *fromcode)
{
    return &input;
}

void
widget_cancel(struct widget *widget gcc_unused)
{
}

bool
widget_check_host(const struct widget *widget gcc_unused,
                  const char *host gcc_unused,
                  const char *site_name gcc_unused)
{
    return true;
}

Session *
session_get(gcc_unused SessionId id)
{
    return NULL;
}

void
session_put(Session *session gcc_unused)
{
}

void
widget_sync_session(struct widget *widget gcc_unused,
                    Session *session gcc_unused)
{
}

void
widget_http_request(gcc_unused struct pool &pool,
                    gcc_unused struct widget &widget,
                    gcc_unused struct processor_env &env,
                    const struct http_response_handler &handler,
                    void *handler_ctx,
                    gcc_unused struct async_operation_ref &async_ref)
{
    GError *error = g_error_new_literal(g_quark_from_static_string("test"), 0,
                                        "Test");
    handler.InvokeAbort(handler_ctx, error);
}

struct test_operation {
    struct async_operation operation;
    struct pool *pool;
};

static void
test_abort(struct async_operation *ao gcc_unused)
{
    struct test_operation *to = (struct test_operation *)ao;

    pool_unref(to->pool);
}

static const struct async_operation_class test_operation = {
    .abort = test_abort,
};

void
widget_resolver_new(struct pool &pool,
                    gcc_unused struct widget &widget,
                    gcc_unused struct tcache &translate_cache,
                    gcc_unused widget_resolver_callback_t callback,
                    gcc_unused void *ctx,
                    struct async_operation_ref &async_ref)
{
    auto to = NewFromPool<struct test_operation>(pool);

    to->pool = &pool;

    to->operation.Init(test_operation);
    async_ref.Set(to->operation);
    pool_ref(&pool);
}

static void
test_abort_resolver(struct pool *pool)
{
    const char *uri;
    bool ret;
    struct parsed_uri parsed_uri;
    struct widget widget;
    EventLoop event_loop;
    struct processor_env env;
    env.event_loop = &event_loop;
    Istream *istream;

    pool = pool_new_linear(pool, "test", 4096);

    uri = "/beng.html";
    ret = parsed_uri.Parse(uri);
    if (!ret) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

    widget.Init(*pool, nullptr);

    istream = embed_inline_widget(*pool, env, false, widget);
    pool_unref(pool);

    istream->CloseUnused();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_abort_resolver(RootPool());
}
