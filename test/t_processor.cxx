#include "FailingResourceLoader.hxx"
#include "processor.hxx"
#include "penv.hxx"
#include "RootPool.hxx"
#include "uri/uri_parser.hxx"
#include "inline_widget.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_lookup.hxx"
#include "rewrite_uri.hxx"
#include "istream/istream.hxx"
#include "istream/istream_block.hxx"
#include "istream/istream_string.hxx"
#include "event/Loop.hxx"
#include "util/Cancellable.hxx"

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

struct tcache *global_translate_cache;

Istream *
embed_inline_widget(struct pool &pool,
                    gcc_unused struct processor_env &env,
                    gcc_unused bool plain_text,
                    Widget &widget)
{
    const char *s = widget.GetIdPath();
    if (s == nullptr)
        s = "widget";

    return istream_string_new(&pool, s);
}

WidgetSession *
widget_get_session(gcc_unused Widget *widget,
                   gcc_unused RealmSession *session,
                   gcc_unused bool create)
{
    return nullptr;
}

enum uri_mode
parse_uri_mode(gcc_unused StringView s)
{
    return URI_MODE_DIRECT;
}

Istream *
rewrite_widget_uri(gcc_unused struct pool &pool,
                   gcc_unused struct processor_env &env,
                   gcc_unused struct tcache &translate_cache,
                   gcc_unused Widget &widget,
                   gcc_unused StringView value,
                   gcc_unused enum uri_mode mode,
                   gcc_unused bool stateful,
                   gcc_unused const char *view,
                   gcc_unused const struct escape_class *escape)
{
    return nullptr;
}

/*
 * WidgetLookupHandler
 *
 */

class MyWidgetLookupHandler final : public WidgetLookupHandler {
public:
    /* virtual methods from class WidgetLookupHandler */
    void WidgetFound(gcc_unused Widget &widget) override {
        g_printerr("widget found\n");
    }

    void WidgetNotFound() override {
        g_printerr("widget not found\n");
    }

    void WidgetLookupError(GError *error) override {
        g_printerr("%s\n", error->message);
        g_error_free(error);
    }
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

    Widget widget(*pool, &root_widget_class);

    SessionId session_id;
    session_id.Generate();

    EventLoop event_loop;
    FailingResourceLoader resource_loader;
    struct processor_env env(pool, event_loop,
                             resource_loader, resource_loader,
                             nullptr, nullptr,
                             "localhost:8080",
                             "localhost:8080",
                             "/beng.html",
                             "http://localhost:8080/beng.html",
                             &parsed_uri,
                             nullptr,
                             "bp_session", session_id, "foo",
                             HTTP_METHOD_GET, nullptr);

    CancellablePointer cancel_ptr;
    MyWidgetLookupHandler handler;
    processor_lookup_widget(*pool, *istream_block_new(*pool),
                            widget, "foo", env, PROCESSOR_CONTAINER,
                            handler, cancel_ptr);

    pool_unref(pool);

    cancel_ptr.Cancel();

    pool_commit();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    test_proxy_abort(RootPool());
}
