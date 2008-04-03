/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "frame.h"
#include "embed.h"
#include "processor.h"
#include "widget.h"
#include "widget-registry.h"

#include <daemon/log.h>

#include <assert.h>

struct widget_callback_ctx {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};

static void
class_lookup_callback(const struct widget_class *class, void *_ctx)
{
    struct widget_callback_ctx *ctx = _ctx;

    if (class == NULL) {
        http_response_handler_invoke_abort(&ctx->handler);
        return;
    }

    ctx->widget->class = class;
    frame_widget_callback(ctx->pool, ctx->env, ctx->widget,
                          ctx->handler.handler, ctx->handler.ctx,
                          ctx->async_ref);
}

static void
frame_top_widget(pool_t pool, struct processor_env *env,
                 struct widget *widget,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref)
{
    unsigned options = 0;

    assert(widget->from_request.proxy);

    if (widget->class == NULL) {
        struct widget_callback_ctx *ctx = p_malloc(env->pool, sizeof(*ctx));
        ctx->pool = pool;
        ctx->env = env;
        ctx->widget = widget;
        http_response_handler_set(&ctx->handler, handler, handler_ctx);
        ctx->async_ref = async_ref;
        widget_class_lookup(env->pool, env->translate_stock, widget->class_name,
                            class_lookup_callback, ctx, async_ref);
        return;
    }

    widget_sync_session(widget);

    switch (widget->display) {
    case WIDGET_DISPLAY_INLINE:
        /* an inline widget is used in a "frame" request - this is
           probably JS requesting new contents for a widget */
        if (widget->class->old_style)
            options = PROCESSOR_FRAGMENT | PROCESSOR_JSCRIPT;
        else
            options = 0;
        break;

    case WIDGET_DISPLAY_IFRAME:
        if (widget->class->old_style)
            options = PROCESSOR_JSCRIPT;
        else
            options = 0;
        break;

    case WIDGET_DISPLAY_NONE:
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

    widget_http_request(pool, widget,
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
    if (widget->class == NULL) {
        struct widget_callback_ctx *ctx = p_malloc(env->pool, sizeof(*ctx));
        ctx->pool = pool;
        ctx->env = env;
        ctx->widget = widget;
        http_response_handler_set(&ctx->handler, handler, handler_ctx);
        ctx->async_ref = async_ref;
        widget_class_lookup(env->pool, env->translate_stock, widget->class_name,
                            class_lookup_callback, ctx, async_ref);
        return;
    }

    widget_sync_session(widget);

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

    widget_http_request(pool, widget,
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
    assert(widget != NULL);
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
        assert(0);
}
