#include "istream.h"
#include "widget.h"
#include "processor.h"
#include "uri.h"
#include "session.h"

#include <stdlib.h>
#include <stdio.h>

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo &c:url; <c:widget id=\"foo\" href=\"http://localhost:8080/foo\"/>");
}

static istream_t
my_embed_widget_callback(pool_t pool, struct processor_env *env,
                         struct widget *widget)
{
    (void)env;

    return istream_string_new(pool,
                              widget->class != NULL && widget->class->uri != NULL
                              ? widget->class->uri : "bar");
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    int ret;
    const char *uri;
    static struct parsed_uri parsed_uri;
    static struct widget_class widget_class;
    static struct widget widget;
    static struct processor_env env;

    uri = "/beng.html";
    ret = uri_parse(pool, &parsed_uri, uri);
    if (ret != 0)
        abort();

    widget_class.uri = NULL;

    widget_init(&widget, &widget_class);

    session_manager_init(pool);

    processor_env_init(pool, &env,
                       NULL,
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       NULL,
                       NULL,
                       NULL,
                       my_embed_widget_callback);

    return processor_new(pool, input, &widget, &env, 0);
}

static void
cleanup(void)
{
    session_manager_deinit();
}

#define FILTER_CLEANUP

#include "t-istream-filter.h"
