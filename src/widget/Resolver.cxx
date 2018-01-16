/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Resolver.hxx"
#include "Registry.hxx"
#include "Widget.hxx"
#include "Class.hxx"
#include "pool/pool.hxx"
#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>

struct WidgetResolver;

struct WidgetResolverListener final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      Cancellable {

    struct pool &pool;

    WidgetResolver &resolver;

    const WidgetResolverCallback callback;

#ifndef NDEBUG
    bool finished = false, aborted = false;
#endif

    WidgetResolverListener(struct pool &_pool, WidgetResolver &_resolver,
                           WidgetResolverCallback _callback,
                           CancellablePointer &cancel_ptr)
        :pool(_pool), resolver(_resolver),
         callback(_callback) {
        cancel_ptr = *this;
    }

    void Finish();

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;
};

struct WidgetResolver {
    Widget &widget;

    boost::intrusive::list<WidgetResolverListener,
                           boost::intrusive::constant_time_size<false>> listeners;

    CancellablePointer cancel_ptr;

    bool finished = false;

#ifndef NDEBUG
    bool running = false;
    bool aborted = false;
#endif

    explicit WidgetResolver(Widget &_widget)
        :widget(_widget) {}

    void Start(struct tcache &translate_cache) {
        /* use the widget pool because the listener pool may be
           aborted, while the others still run */
        widget_class_lookup(widget.pool, widget.pool, translate_cache,
                            widget.class_name,
                            BIND_THIS_METHOD(RegistryCallback),
                            cancel_ptr);
    }

    void RemoveListener(WidgetResolverListener &listener);
    void Abort();

    void RegistryCallback(const WidgetClass *cls);
};

void
WidgetResolver::RemoveListener(WidgetResolverListener &listener)
{
    listeners.erase(listeners.iterator_to(listener));

    if (listeners.empty() && !finished)
        /* the last listener has been aborted: abort the widget
           registry */
        Abort();
}

void
WidgetResolver::Abort()
{
    assert(listeners.empty());
    assert(widget.resolver == this);

#ifndef NDEBUG
    aborted = true;
#endif

    widget.resolver = nullptr;
    cancel_ptr.Cancel();
    pool_unref(&widget.pool);
}

/*
 * async operation
 *
 */

void
WidgetResolverListener::Cancel() noexcept
{
    assert(!finished);
    assert(!aborted);
    assert(resolver.widget.resolver == &resolver);
    assert(!resolver.listeners.empty());
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

    callback();
    pool_unref(&pool);
}

void
WidgetResolver::RegistryCallback(const WidgetClass *cls)
{
    assert(widget.cls == nullptr);
    assert(widget.resolver == this);
    assert(!listeners.empty());
    assert(!finished);
    assert(!running);
    assert(!aborted);

    finished = true;

#ifndef NDEBUG
    running = true;
#endif

    widget.cls = cls;

    widget.from_template.view = widget.from_request.view = cls != nullptr
        ? widget_view_lookup(&cls->views, widget.from_template.view_name)
        : nullptr;

    widget.session_sync_pending = cls != nullptr && cls->stateful &&
        /* the widget session code requires a valid view */
        widget.from_template.view != nullptr;

    do {
        auto &l = listeners.front();
        listeners.pop_front();
        l.Finish();
    } while (!listeners.empty());

#ifndef NDEBUG
    running = false;
#endif

    pool_unref(&widget.pool);
}


/*
 * constructor
 *
 */

static WidgetResolver *
widget_resolver_alloc(Widget &widget)
{
    auto &pool = widget.pool;

    pool_ref(&pool);

    return widget.resolver = NewFromPool<WidgetResolver>(pool, widget);
}

void
ResolveWidget(struct pool &pool,
              Widget &widget,
              struct tcache &translate_cache,
              WidgetResolverCallback callback,
              CancellablePointer &cancel_ptr)
{
    bool is_new = false;

    assert(widget.class_name != nullptr);
    assert(pool_contains(widget.pool, &widget, sizeof(widget)));

    if (widget.cls != nullptr) {
        /* already resolved successfully */
        callback();
        return;
    }

    /* create new resolver object if it does not already exist */

    WidgetResolver *resolver = widget.resolver;
    if (resolver == nullptr) {
        resolver = widget_resolver_alloc(widget);
        is_new = true;
    } else if (resolver->finished) {
        /* we have already failed to resolve this widget class; return
           immediately, don't try again */
        callback();
        return;
    }

    assert(pool_contains(widget.pool, widget.resolver,
                         sizeof(*widget.resolver)));

    /* add a new listener to the resolver */

    pool_ref(&pool);
    auto listener = NewFromPool<WidgetResolverListener>(pool, pool, *resolver,
                                                        callback,
                                                        cancel_ptr);

    resolver->listeners.push_back(*listener);

    /* finally send request to the widget registry */

    if (is_new)
        resolver->Start(translate_cache);
}
