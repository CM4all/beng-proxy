/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "embed.h"
#include "processor.h"
#include "widget.h"

#include <daemon/log.h>

#include <assert.h>

static void
frame_top_widget(pool_t pool, struct processor_env *env,
                 struct widget *widget,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    unsigned options = 0;

    assert(widget->from_request.proxy);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        /* an inline widget is used in a "frame" request - this is
           probably JS requesting new contents for a widget */
        options = PROCESSOR_BODY | PROCESSOR_JSCRIPT | PROCESSOR_JSCRIPT;
        break;

    case WIDGET_DISPLAY_IFRAME:
        options = PROCESSOR_JSCRIPT | PROCESSOR_JSCRIPT_ROOT;
        break;

    case WIDGET_DISPLAY_IMG:
        options = 0;
        break;

    case WIDGET_DISPLAY_EXTERNAL:
        {
            struct http_response_handler_ref handler_ref;
            http_response_handler_set(&handler_ref, handler, handler_ctx);
            http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_NO_CONTENT,
                                                  NULL, NULL);
        }
        return;
    }

    embed_new(pool, widget,
              env, options,
              handler, handler_ctx, async_ref);
}

static void
frame_parent_widget(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    if (!widget->class->is_container) {
        /* this widget cannot possibly be the parent of a framed
           widget if it is not a container */
        struct http_response_handler_ref handler_ref;

        daemon_log(4, "frame within non-container requested\n");

        if (env->request_body != NULL)
            istream_free(&env->request_body);

        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_abort(&handler_ref);
        return;
    }

    if (env->request_body != NULL && widget->from_request.focus_ref == NULL) {
        /* the request body is not consumed yet, but the focus is not
           within the frame: discard the body, because it cannot ever
           be used */
        assert(!istream_has_handler(env->request_body));

        daemon_log(4, "discarding non-framed request body\n");

        istream_free(&env->request_body);
    }

    embed_new(pool, widget,
              env, 0,
              handler, handler_ctx, async_ref);
}

void
frame_widget_callback(pool_t pool, struct processor_env *env,
                      struct widget *widget,
                      const struct http_response_handler *handler,
                      void *handler_ctx,
                      struct async_operation_ref *async_ref)
{
    assert(pool != NULL);
    assert(env != NULL);
    assert(env->widget_callback == frame_widget_callback);
    assert(widget != NULL);
    assert(widget->class != NULL);
    assert(widget->from_request.proxy || widget->from_request.proxy_ref != NULL ||
           widget->parent != NULL);

    if (widget->from_request.proxy)
        /* this widget is being proxied */
        frame_top_widget(pool, env, widget,
                         handler, handler_ctx, async_ref);
    else if (widget->from_request.proxy_ref != NULL)
        /* only partial match: this is the parent of the frame
           widget */
        frame_parent_widget(pool, env, widget,
                            handler, handler_ctx, async_ref);
    else if (widget->parent->from_request.proxy_ref != NULL) {
        struct http_response_handler_ref handler_ref;

        /* this widget is none of our business */
        widget_cancel(widget);

        http_response_handler_set(&handler_ref, handler, handler_ctx);
        http_response_handler_invoke_response(&handler_ref, HTTP_STATUS_NO_CONTENT,
                                              NULL, NULL);
    } else
        /* child of a proxied widget */
        embed_widget_callback(pool, env, widget, handler, handler_ctx, async_ref);
}
