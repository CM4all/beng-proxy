#include "istream.h"
#include "widget.h"
#include "processor.h"
#include "uri.h"
#include "session.h"
#include "widget-stream.h"

#include <stdlib.h>
#include <stdio.h>

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo &c:url; <c:widget id=\"foo\" href=\"http://localhost:8080/foo\"/>");
}

static void
my_embed_widget_callback(pool_t pool, struct processor_env *env,
                         struct widget *widget,
                         const struct http_response_handler *handler,
                         void *handler_ctx,
                         struct async_operation_ref *async_ref)
{
    struct http_response_handler_ref handler_ref;
    istream_t body;

    (void)env;
    (void)async_ref;

    http_response_handler_set(&handler_ref, handler, handler_ctx);

    body = istream_string_new(pool,
                              widget->class->uri != NULL
                              ? widget->class->uri : "bar");

    http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_OK, NULL,
                                          body);
}

static istream_t
create_test(pool_t pool, istream_t input)
{
    int ret;
    const char *uri;
    static struct parsed_uri parsed_uri;
    static struct widget widget;
    static struct processor_env env;
    struct widget_stream *ws;
    istream_t delayed;

    uri = "/beng.html";
    ret = uri_parse(pool, &parsed_uri, uri);
    if (ret != 0)
        abort();

    widget_init(&widget, &root_widget_class);

    session_manager_init(pool);

    processor_env_init(pool, &env,
                       NULL,
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       session_new(),
                       NULL,
                       NULL,
                       my_embed_widget_callback);

    ws = widget_stream_new(pool);
    delayed = ws->delayed;

    processor_new(pool, input, &widget, &env, PROCESSOR_CONTAINER,
                  &widget_stream_response_handler, ws,
                  &ws->async_ref);

    return delayed;
}

static void
cleanup(void)
{
    session_manager_deinit();
}

#define FILTER_CLEANUP

#include "t-istream-filter.h"
