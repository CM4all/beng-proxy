#include "inline_widget.hxx"
#include "uri/uri_parser.hxx"
#include "widget.hxx"
#include "widget_http.hxx"
#include "widget_resolver.hxx"
#include "processor.hxx"
#include "penv.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"
#include "istream/istream_iconv.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "session.hxx"
#include "event/Loop.hxx"
#include "util/Cancellable.hxx"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

const char *
Widget::GetLogName() const
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
Widget::Cancel()
{
}

void
Widget::CheckHost(const char *, const char *) const
{
}

RealmSessionLease
processor_env::GetRealmSession() const
{
    return nullptr;
}

void
session_put(Session *session gcc_unused)
{
}

void
Widget::LoadFromSession(gcc_unused RealmSession &session)
{
}

void
widget_http_request(gcc_unused struct pool &pool,
                    gcc_unused Widget &widget,
                    gcc_unused struct processor_env &env,
                    HttpResponseHandler &handler,
                    gcc_unused CancellablePointer &cancel_ptr)
{
    GError *error = g_error_new_literal(g_quark_from_static_string("test"), 0,
                                        "Test");
    handler.InvokeError(error);
}

struct TestOperation final : Cancellable {
    struct pool *pool;

    TestOperation(struct pool &_pool):pool(&_pool) {
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        pool_unref(pool);
    }
};

void
ResolveWidget(struct pool &pool,
              gcc_unused Widget &widget,
              gcc_unused struct tcache &translate_cache,
              gcc_unused WidgetResolverCallback callback,
              CancellablePointer &cancel_ptr)
{
    auto to = NewFromPool<TestOperation>(pool, pool);
    cancel_ptr = *to;
    pool_ref(&pool);
}

static void
test_abort_resolver(struct pool *pool)
{
    const char *uri;
    bool ret;
    struct parsed_uri parsed_uri;
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

    Widget widget(*pool, nullptr);

    istream = embed_inline_widget(*pool, env, false, widget);
    pool_unref(pool);

    istream->CloseUnused();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_abort_resolver(RootPool());
}
