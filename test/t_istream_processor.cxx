#include "istream.h"
#include "widget.h"
#include "widget_class.hxx"
#include "processor.h"
#include "penv.hxx"
#include "uri-parser.h"
#include "session_manager.h"
#include "inline_widget.hxx"
#include "widget_registry.hxx"
#include "http_address.h"
#include "global.h"
#include "crash.h"

#include <stdlib.h>
#include <stdio.h>

#define EXPECTED_RESULT "foo &c:url; <script><c:widget id=\"foo\" type=\"bar\"/></script> bar"

void
widget_class_lookup(gcc_unused struct pool *pool, gcc_unused struct pool *widget_pool,
                    gcc_unused struct tcache *translate_cache,
                    gcc_unused const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    gcc_unused struct async_operation_ref *async_ref)
{
    callback(nullptr, ctx);
}

struct istream *
embed_inline_widget(struct pool *pool, gcc_unused struct processor_env *env,
                    struct widget *widget)
{
    return istream_string_new(pool, p_strdup(pool, widget->class_name));
}

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo &c:url; <script><c:widget id=\"foo\" type=\"bar\"/></script> <c:widget id=\"foo\" type=\"bar\"/>");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    bool ret;
    const char *uri;
    static struct parsed_uri parsed_uri;
    static struct widget widget;
    static struct processor_env env;
    struct session *session;

    /* HACK, processor.c will ignore c:widget otherwise */
    global_translate_cache = (struct tcache *)(size_t)1;

    uri = "/beng.html";
    ret = uri_parse(&parsed_uri, uri);
    if (!ret)
        abort();

    widget_init(&widget, pool, &root_widget_class);

    crash_global_init();
    session_manager_init(1200, 0, 0);

    session = session_new();
    processor_env_init(pool, &env,
                       nullptr, nullptr,
                       "localhost:8080",
                       "localhost:8080",
                       "/beng.html",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       nullptr,
                       session->id,
                       HTTP_METHOD_GET, nullptr);
    session_put(session);

    return processor_process(pool, input, &widget, &env, PROCESSOR_CONTAINER);
}

static void
cleanup(void)
{
    session_manager_deinit();
    crash_global_deinit();
}

#define FILTER_CLEANUP

#include "t-istream-filter.h"
