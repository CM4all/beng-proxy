/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-registry.h"
#include "processor.h"
#include "widget.h"

void
widget_registry_lookup(pool_t pool,
                       struct stock *translate_stock,
                       const char *widget_type,
                       translate_callback_t callback,
                       void *ctx,
                       struct async_operation_ref *async_ref)
{
    struct translate_request *request = p_malloc(pool, sizeof(*request)); 

    request->remote_host = NULL;
    request->host = NULL;
    request->uri = NULL;
    request->widget_type = widget_type;
    request->session = NULL;
    request->param = NULL;

    translate(pool, translate_stock, request,
              callback, ctx, async_ref);
}

struct widget_class_lookup {
    pool_t pool;

    struct processor_env *env;
    struct widget *widget;

    struct http_response_handler_ref handler;

    struct async_operation_ref *async_ref;
};

static void 
lookup_callback(const struct translate_response *response, void *ctx)
{
    struct widget_class_lookup *lookup = ctx;
    struct widget_class *class;

    if (response->status != 0) {
        /* XXX */
        http_response_handler_invoke_response(&lookup->handler, response->status,
                                              NULL, NULL);
        pool_unref(lookup->pool);
        return;
    }

    assert(response->proxy != NULL); /* XXX */

    class = p_malloc(lookup->pool, sizeof(*class));
    class->uri = response->proxy;
    class->old_style = 0;

    if (response->transformation != NULL &&
        response->transformation->type == TRANSFORMATION_PROCESS) {
        class->type = WIDGET_TYPE_BENG;
        class->is_container = (response->transformation->u.processor_options & PROCESSOR_CONTAINER) != 0;
    } else {
        class->type = WIDGET_TYPE_RAW;
        class->is_container = 0;
    }

    lookup->widget->class = class;

    lookup->env->widget_callback(lookup->pool, lookup->env, lookup->widget,
                                 lookup->handler.handler, lookup->handler.ctx, 
                                 lookup->async_ref);
    pool_unref(lookup->pool);
}

void
widget_class_lookup(pool_t pool, struct processor_env *env,
                    struct widget *widget,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_class_lookup *lookup = p_malloc(pool, sizeof(*lookup));

    assert(widget->class == NULL);
    assert(widget->class_name != NULL);

    pool_ref(pool);

    lookup->pool = pool;
    lookup->env = env;
    lookup->widget = widget;
    http_response_handler_set(&lookup->handler, handler, handler_ctx);
    lookup->async_ref = async_ref;

    widget_registry_lookup(pool, env->translate_stock, widget->class_name,
                           lookup_callback, lookup,
                           async_ref);
}
