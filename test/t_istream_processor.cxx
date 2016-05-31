#include "FailingResourceLoader.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "processor.hxx"
#include "penv.hxx"
#include "uri/uri_parser.hxx"
#include "session_manager.hxx"
#include "inline_widget.hxx"
#include "widget_registry.hxx"
#include "bp_global.hxx"
#include "crash.hxx"
#include "session.hxx"

#include <stdlib.h>
#include <stdio.h>

#define EXPECTED_RESULT "foo &c:url; <script><c:widget id=\"foo\" type=\"bar\"/></script> bar"

class EventLoop;

const struct timeval inline_widget_timeout = {
    .tv_sec = 10,
    .tv_usec = 0,
};

void
widget_class_lookup(gcc_unused struct pool &pool,
                    gcc_unused struct pool &widget_pool,
                    gcc_unused struct tcache &translate_cache,
                    gcc_unused const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    gcc_unused struct async_operation_ref &async_ref)
{
    callback(nullptr, ctx);
}

Istream *
embed_inline_widget(struct pool &pool, gcc_unused struct processor_env &env,
                    gcc_unused bool plain_text,
                    struct widget &widget)
{
    return istream_string_new(&pool, p_strdup(&pool, widget.class_name));
}

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo &c:url; <script><c:widget id=\"foo\" type=\"bar\"/></script> <c:widget id=\"foo\" type=\"bar\"/>");
}

static Istream *
create_test(EventLoop &event_loop, struct pool *pool, Istream *input)
{
    bool ret;
    const char *uri;
    static struct parsed_uri parsed_uri;
    static struct widget widget;

    /* HACK, processor.c will ignore c:widget otherwise */
    global_translate_cache = (struct tcache *)(size_t)1;

    uri = "/beng.html";
    ret = parsed_uri.Parse(uri);
    if (!ret)
        abort();

    widget.Init(*pool, &root_widget_class);

    crash_global_init();
    session_manager_init(event_loop, 1200, 0, 0);

    auto *session = session_new("");

    static struct processor_env env;
    FailingResourceLoader resource_loader;
    env = processor_env(pool, event_loop, resource_loader, resource_loader,
                        nullptr, nullptr,
                        "localhost:8080",
                        "localhost:8080",
                        "/beng.html",
                        "http://localhost:8080/beng.html",
                        &parsed_uri,
                        nullptr,
                        "bp_session", session->id,
                        HTTP_METHOD_GET, nullptr);
    session_put(session);

    return processor_process(*pool, *input, widget, env, PROCESSOR_CONTAINER);
}

static void
cleanup(void)
{
    session_manager_deinit();
    crash_global_deinit();
}

#define FILTER_CLEANUP

#include "t_istream_filter.hxx"
