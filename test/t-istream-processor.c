#include "istream.h"
#include "widget.h"
#include "processor.h"
#include "uri-parser.h"
#include "session.h"
#include "widget-stream.h"
#include "embed.h"
#include "widget-registry.h"
#include "uri-address.h"
#include "global.h"

#include <stdlib.h>
#include <stdio.h>

#define EXPECTED_RESULT "foo &c:url; "

void
widget_class_lookup(pool_t pool __attr_unused, pool_t widget_pool __attr_unused,
                    struct tcache *translate_cache __attr_unused,
                    const char *widget_type __attr_unused,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref __attr_unused)
{
    callback(NULL, ctx);
}

istream_t
embed_inline_widget(pool_t pool, struct processor_env *env __attr_unused,
                    struct widget *widget)
{
    return istream_string_new(pool,
                              widget->class->address.type == RESOURCE_ADDRESS_HTTP
                              ? widget->class->address.u.http->uri : "bar");
}

void
embed_frame_widget(pool_t pool __attr_unused,
                   struct processor_env *env __attr_unused,
                   struct widget *widget __attr_unused,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref __attr_unused)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

static istream_t
create_input(pool_t pool)
{
    return istream_string_new(pool, "foo &c:url; <c:widget id=\"foo\" type=\"bar\"/>");
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

    /* HACK, processor.c will ignore c:widget otherwise */
    global_translate_cache = (struct tcache *)(size_t)1;

    uri = "/beng.html";
    ret = uri_parse(pool, &parsed_uri, uri);
    if (ret != 0)
        abort();

    widget_init(&widget, &root_widget_class);

    session_manager_init();

    processor_env_init(pool, &env,
                       NULL,
                       "localhost:8080",
                       "http://localhost:8080/beng.html",
                       &parsed_uri,
                       NULL,
                       session_new()->uri_id,
                       NULL,
                       NULL);

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
