#include "StdioSink.hxx"
#include "fb_pool.hxx"
#include "processor.hxx"
#include "penv.hxx"
#include "uri/uri_parser.hxx"
#include "inline_widget.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "rewrite_uri.hxx"
#include "istream/istream_file.hxx"
#include "istream/istream_string.hxx"
#include "util/StringView.hxx"

#include <event.h>

/*
 * emulate missing libraries
 *
 */

struct tcache *global_translate_cache;

Istream *
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
parse_uri_mode(gcc_unused StringView s)
{
    return URI_MODE_DIRECT;
}

Istream *
rewrite_widget_uri(gcc_unused struct pool &pool,
                   gcc_unused struct pool &widget_pool,
                   gcc_unused struct processor_env &env,
                   gcc_unused struct tcache &translate_cache,
                   gcc_unused struct widget &widget,
                   gcc_unused StringView value,
                   gcc_unused enum uri_mode mode,
                   gcc_unused bool stateful,
                   gcc_unused const char *view,
                   gcc_unused const struct escape_class *escape)
{
    return nullptr;
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;
    const char *uri;
    bool ret;
    struct parsed_uri parsed_uri;
    struct widget widget;

    (void)argc;
    (void)argv;

    event_base = event_init();
    fb_pool_init(false);

    pool = pool_new_libc(nullptr, "root");

    uri = "/beng.html";
    ret = parsed_uri.Parse(uri);
    if (!ret) {
        fprintf(stderr, "uri_parse() failed\n");
        exit(2);
    }

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
                             nullptr,
                             session_id,
                             HTTP_METHOD_GET, nullptr);

    Istream *result =
        processor_process(*pool,
                          *istream_file_new(pool, "/dev/stdin", (off_t)-1,
                                            NULL),
                          widget, env, PROCESSOR_CONTAINER);

    StdioSink sink(*result);
    sink.LoopRead();

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    fb_pool_deinit();
    event_base_free(event_base);
}
