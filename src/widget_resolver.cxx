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

    struct pool &pool;

    WidgetResolver &resolver;

    struct async_operation operation;

    widget_resolver_callback_t callback;

    void *callback_ctx;

#ifndef NDEBUG
    bool finished = false, aborted = false;
#endif

    WidgetResolverListener(struct pool &_pool, WidgetResolver &_resolver,
                           widget_resolver_callback_t _callback, void *_ctx,
                           struct async_operation_ref &async_ref)
        :pool(_pool), resolver(_resolver),
         callback(_callback), callback_ctx(_ctx) {
        operation.Init2<WidgetResolverListener>();
        async_ref.Set(operation);
    }

    void Finish();

    void Abort();
};

struct WidgetResolver {
    struct widget &widget;

    struct list_head listeners;

    struct async_operation_ref async_ref;

    bool finished = false;

#ifndef NDEBUG
    bool running = false;
    bool aborted = false;
#endif

    explicit WidgetResolver(struct widget &_widget)
        :widget(_widget) {
        list_init(&listeners);
    }

    void RemoveListener(WidgetResolverListener &listener);
    void Abort();
};

void
WidgetResolver::RemoveListener(WidgetResolverListener &listener)
{
    list_remove(&listener.siblings);

    if (list_empty(&listeners) && !finished)
        /* the last listener has been aborted: abort the widget
           registry */
        Abort();
}

void
WidgetResolver::Abort()
{
    assert(list_empty(&listeners));
    assert(widget.resolver == this);

#ifndef NDEBUG
    aborted = true;
#endif

    widget.resolver = nullptr;
    async_ref.Abort();
    pool_unref(widget.pool);
}

/*
 * async operation
 *
 */

inline void
WidgetResolverListener::Abort()
{
    assert(!finished);
    assert(!aborted);
    assert(resolver.widget.resolver == &resolver);
    assert(!list_empty(&resolver.listeners));
    assert(!resolver.finished || resolver.running);
    assert(!resolver.aborted);

#ifndef NDEBUG
    aborted = true;
#endif

    resolver.RemoveListener(*this);

    pool_unref(&pool);
}


/*
 * registry callback
 *
 */

inline void
WidgetResolverListener::Finish()
{
    assert(!finished);
    assert(!aborted);

#ifndef NDEBUG
    finished = true;
#endif

    operation.Finished();
    callback(callback_ctx);
    pool_unref(&pool);
}

static void
widget_resolver_callback(const WidgetClass *cls, void *ctx)
{
    auto &widget = *(struct widget *)ctx;
    assert(widget.cls == nullptr);
    assert(widget.resolver != nullptr);

    auto &resolver = *widget.resolver;

    assert(&resolver.widget == &widget);
    assert(!list_empty(&resolver.listeners));
    assert(!resolver.finished);
    assert(!resolver.running);
    assert(!resolver.aborted);

    resolver.finished = true;

#ifndef NDEBUG
    resolver.running = true;
#endif

    widget.cls = cls;

    widget.view = widget.from_request.view = cls != nullptr
        ? widget_view_lookup(&cls->views, widget.view_name)
        : nullptr;

    widget.session_sync_pending = cls != nullptr && cls->stateful &&
        /* the widget session code requires a valid view */
        widget.view != nullptr;

    do {
        auto &listener =
            *(WidgetResolverListener *)resolver.listeners.next;

        list_remove(&listener.siblings);
        listener.Finish();
    } while (!list_empty(&resolver.listeners));

#ifndef NDEBUG
    resolver.running = false;
#endif

    pool_unref(resolver.widget.pool);
}


/*
 * constructor
 *
 */

static WidgetResolver *
widget_resolver_alloc(struct widget &widget)
{
    auto &pool = *widget.pool;

    pool_ref(&pool);

    return widget.resolver = NewFromPool<WidgetResolver>(pool, widget);
}

void
widget_resolver_new(struct pool &pool,
                    struct widget &widget,
                    struct tcache &translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    struct async_operation_ref &async_ref)
{
    bool is_new = false;

    assert(widget.class_name != nullptr);
    assert(widget.cls == nullptr);
    assert(pool_contains(widget.pool, &widget, sizeof(widget)));

    /* create new resolver object if it does not already exist */

    WidgetResolver *resolver = widget.resolver;
    if (resolver == nullptr) {
        resolver = widget_resolver_alloc(widget);
        is_new = true;
    } else if (resolver->finished) {
        /* we have already failed to resolve this widget class; return
           immediately, don't try again */
        callback(ctx);
        return;
    }

    assert(pool_contains(widget.pool, widget.resolver,
                         sizeof(*widget.resolver)));

    /* add a new listener to the resolver */

    pool_ref(&pool);
    auto listener = NewFromPool<WidgetResolverListener>(pool, pool, *resolver,
                                                        callback, ctx,
                                                        async_ref);

    list_add(&listener->siblings, resolver->listeners.prev);

    /* finally send request to the widget registry */

    if (is_new)
        /* don't pass "pool" here because the listener pool may be
           aborted, while the others still run */
        widget_class_lookup(*widget.pool, *widget.pool, translate_cache,
                            widget.class_name,
                            widget_resolver_callback, &widget,
                            resolver->async_ref);
}
