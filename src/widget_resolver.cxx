/*
 * Wrapper for widget_registry.hxx which resolves widget classes.
 * This library can manage several concurrent requests for one widget
 * object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_resolver.hxx"
#include "widget_registry.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <inline/list.h>

struct WidgetResolver;

struct WidgetResolverListener {
    struct list_head siblings;

    struct pool *pool;

    WidgetResolver *resolver;

    struct async_operation operation;

    widget_resolver_callback_t callback;

    void *callback_ctx;

#ifndef NDEBUG
    bool listed, finished, aborted;
#endif
};

struct WidgetResolver {
    struct pool *pool;

    struct widget *widget;

    struct list_head listeners;

#ifndef NDEBUG
    unsigned num_listeners;
#endif

    struct async_operation_ref async_ref;

    bool finished;

#ifndef NDEBUG
    bool running;
    bool aborted;
#endif
};


/*
 * async operation
 *
 */

static WidgetResolverListener *
async_to_wrl(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &WidgetResolverListener::operation);
}

static void
wrl_abort(struct async_operation *ao)
{
    WidgetResolverListener *listener = async_to_wrl(ao);
    WidgetResolver *resolver = listener->resolver;

    assert(listener->listed);
    assert(!listener->finished);
    assert(!listener->aborted);
    assert(resolver->widget->resolver == resolver);
    assert(!list_empty(&resolver->listeners));
    assert(!resolver->finished || resolver->running);
    assert(!resolver->aborted);

    assert(resolver->num_listeners > 0);
#ifndef NDEBUG
    --resolver->num_listeners;
    listener->listed = false;
    listener->aborted = true;
#endif

    list_remove(&listener->siblings);
    pool_unref(listener->pool);

    if (list_empty(&resolver->listeners) && !resolver->finished) {
        /* the last listener has been aborted: abort the widget
           registry */
        assert(resolver->num_listeners == 0);

#ifndef NDEBUG
        resolver->aborted = true;
#endif

        resolver->widget->resolver = nullptr;
        resolver->async_ref.Abort();
        pool_unref(resolver->pool);
    }
}

static const struct async_operation_class listener_async_operation = {
    .abort = wrl_abort,
};


/*
 * registry callback
 *
 */

static void
widget_resolver_callback(const WidgetClass *cls, void *ctx)
{
    struct widget *widget = (struct widget *)ctx;
    WidgetResolver *resolver = widget->resolver;

    assert(widget->cls == nullptr);
    assert(resolver != nullptr);
    assert(resolver->widget == widget);
    assert(!list_empty(&resolver->listeners));
    assert(!resolver->finished);
    assert(!resolver->running);
    assert(!resolver->aborted);

    resolver->finished = true;

#ifndef NDEBUG
    resolver->running = true;
#endif

    widget->cls = cls;

    widget->view = widget->from_request.view = cls != nullptr
        ? widget_view_lookup(&cls->views, widget->view_name)
        : nullptr;

    widget->session_sync_pending = cls != nullptr && cls->stateful &&
        /* the widget session code requires a valid view */
        widget->view != nullptr;

    do {
        WidgetResolverListener *listener =
            (WidgetResolverListener *)resolver->listeners.next;

        assert(listener->listed);
        assert(!listener->finished);
        assert(!listener->aborted);

        assert(resolver->num_listeners > 0);
#ifndef NDEBUG
        --resolver->num_listeners;
        listener->listed = false;
        listener->finished = true;
#endif

        list_remove(&listener->siblings);

        listener->operation.Finished();
        listener->callback(listener->callback_ctx);
        pool_unref(listener->pool);
    } while (!list_empty(&resolver->listeners));

#ifndef NDEBUG
    resolver->running = false;
#endif

    assert(resolver->num_listeners == 0);

    pool_unref(resolver->pool);
}


/*
 * constructor
 *
 */

static WidgetResolver *
widget_resolver_alloc(struct pool &pool, struct widget &widget)
{
    auto resolver = NewFromPool<WidgetResolver>(pool);

    pool_ref(&pool);

    resolver->pool = &pool;
    resolver->widget = &widget;
    list_init(&resolver->listeners);

    resolver->finished = false;

#ifndef NDEBUG
    resolver->num_listeners = 0;
    resolver->running = false;
    resolver->aborted = false;
#endif

    widget.resolver = resolver;

    return resolver;
}

void
widget_resolver_new(struct pool &pool, struct pool &widget_pool,
                    struct widget &widget,
                    struct tcache &translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    struct async_operation_ref &async_ref)
{
    WidgetResolver *resolver;
    bool is_new = false;

    assert(widget.class_name != nullptr);
    assert(widget.cls == nullptr);
    assert(pool_contains(&widget_pool, &widget, sizeof(widget)));

    /* create new resolver object if it does not already exist */

    resolver = widget.resolver;
    if (resolver == nullptr) {
        resolver = widget_resolver_alloc(widget_pool, widget);
        is_new = true;
    } else if (resolver->finished) {
        /* we have already failed to resolve this widget class; return
           immediately, don't try again */
        callback(ctx);
        return;
    }

    assert(resolver->pool == &widget_pool);
    assert(pool_contains(&widget_pool, widget.resolver,
                         sizeof(*widget.resolver)));

    /* add a new listener to the resolver */

    pool_ref(&pool);
    auto listener = NewFromPool<WidgetResolverListener>(pool);
    listener->pool = &pool;
    listener->resolver = resolver;

    listener->operation.Init(listener_async_operation);
    async_ref.Set(listener->operation);

    listener->callback = callback;
    listener->callback_ctx = ctx;

#ifndef NDEBUG
    listener->listed = true;
    listener->finished = false;
    listener->aborted = false;
#endif

    list_add(&listener->siblings, resolver->listeners.prev);

#ifndef NDEBUG
    ++resolver->num_listeners;
#endif

    /* finally send request to the widget registry */

    if (is_new)
        /* don't pass "pool" here because the listener pool may be
           aborted, while the others still run */
        widget_class_lookup(widget_pool, widget_pool, translate_cache,
                            widget.class_name,
                            widget_resolver_callback, &widget,
                            resolver->async_ref);
}
