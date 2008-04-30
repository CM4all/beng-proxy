/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-registry.h"
#include "processor.h"
#include "widget.h"
#include "tcache.h"
#include "uri-address.h"

static void
widget_registry_lookup(pool_t pool,
                       struct tcache *tcache,
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

    translate_cache(pool, tcache, request,
                    callback, ctx, async_ref);
}

struct widget_class_lookup {
    pool_t pool;

    struct widget_class class;

    widget_class_callback_t callback;
    void *callback_ctx;
};

static void 
lookup_callback(const struct translate_response *response, void *ctx)
{
    struct widget_class_lookup *lookup = ctx;
    struct widget_class *class;

    if (response->status != 0) {
        lookup->callback(NULL, lookup->callback_ctx);
        return;
    }

    assert(response->proxy != NULL); /* XXX */

    class = &lookup->class;
    class->address = uri_address_dup(lookup->pool, response->proxy);
    class->old_style = false;

    if (response->transformation != NULL &&
        response->transformation->type == TRANSFORMATION_PROCESS) {
        class->type = WIDGET_TYPE_BENG;
        class->is_container = (response->transformation->u.processor_options & PROCESSOR_CONTAINER) != 0;
    } else if (response->google_gadget) {
        class->type = WIDGET_TYPE_GOOGLE_GADGET;
        class->is_container = false;
    } else {
        class->type = WIDGET_TYPE_RAW;
        class->is_container = false;
    }

    lookup->callback(class, lookup->callback_ctx);
}


void
widget_class_lookup(pool_t pool,
                    struct tcache *tcache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_class_lookup *lookup = p_malloc(pool, sizeof(*lookup));

    assert(widget_type != NULL);

    lookup->pool = pool;
    lookup->callback = callback;
    lookup->callback_ctx = ctx;

    widget_registry_lookup(pool, tcache, widget_type,
                           lookup_callback, lookup,
                           async_ref);
}
