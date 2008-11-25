/*
 * Wrapper for widget-registry.h which resolves widget classes.  This
 * library can manage several concurrent requests for one widget
 * object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-resolver.h"
#include "widget-registry.h"
#include "widget.h"
#include "async.h"

#include <inline/list.h>

struct widget_resolver_listener {
    struct list_head siblings;

    pool_t pool;

    struct widget_resolver *resolver;

    struct async_operation operation;

    widget_resolver_callback_t callback;

    void *callback_ctx;
};

struct widget_resolver {
    struct list_head listeners;

    pool_t pool;

    struct async_operation_ref async_ref;
};


/*
 * async operation
 *
 */

static struct widget_resolver_listener *
async_to_wrl(struct async_operation *ao)
{
    return (struct widget_resolver_listener*)(((char*)ao) - offsetof(struct widget_resolver_listener, operation));
}

static void
wrl_abort(struct async_operation *ao)
{
    struct widget_resolver_listener *listener = async_to_wrl(ao);
    struct widget_resolver *resolver = listener->resolver;

    list_remove(&listener->siblings);
    if (list_empty(&resolver->listeners)) {
        /* the last listener has been aborted: abort the widget
           registry */
        async_abort(&resolver->async_ref);
        pool_unref(resolver->pool);
    }

    pool_unref(listener->pool);
}

static const struct async_operation_class listener_async_operation = {
    .abort = wrl_abort,
};


/*
 * registry callback
 *
 */

static void
widget_resolver_callback(const struct widget_class *class, void *ctx)
{
    struct widget *widget = ctx;
    struct widget_resolver *resolver = widget->resolver;

    assert(widget->class == NULL);
    assert(resolver != NULL);
    assert(!list_empty(&resolver->listeners));

    widget->class = class;

    do {
        struct widget_resolver_listener *listener =
            (struct widget_resolver_listener *)resolver->listeners.next;

        list_remove(&listener->siblings);
        listener->callback(listener->callback_ctx);
        pool_unref(listener->pool);
    } while (!list_empty(&resolver->listeners));

    pool_unref(resolver->pool);
}


/*
 * constructor
 *
 */

void
widget_resolver_new(pool_t pool, pool_t widget_pool, struct widget *widget,
                    struct tcache *translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_resolver *resolver;
    struct widget_resolver_listener *listener;
    bool new = false;

    assert(widget != NULL);
    assert(widget->class_name != NULL);
    assert(widget->class == NULL);
    assert(pool_contains(widget_pool, widget, sizeof(*widget)));

    /* create new resolver object if it does not already exist */

    resolver = widget->resolver;
    if (resolver == NULL) {
        pool_ref(widget_pool);
        resolver = p_malloc(widget_pool, sizeof(*widget->resolver));
        list_init(&resolver->listeners);
        resolver->pool = widget_pool;
        widget->resolver = resolver;
        new = true;
    }

    assert(pool_contains(widget_pool, widget->resolver,
                         sizeof(*widget->resolver)));

    /* add a new listener to the resolver */

    pool_ref(pool);
    listener = p_malloc(pool, sizeof(*listener));
    listener->pool = pool;
    listener->resolver = resolver;

    async_init(&listener->operation, &listener_async_operation);
    async_ref_set(async_ref, &listener->operation);

    listener->callback = callback;
    listener->callback_ctx = ctx;

    list_add(&listener->siblings, &resolver->listeners);

    /* finally send request to the widget registry */

    if (new)
        widget_class_lookup(pool, widget_pool, translate_cache,
                            widget->class_name,
                            widget_resolver_callback, widget,
                            &resolver->async_ref);
}
