/*
 * Copyright 2007-2019 Content Management AG
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
#include "pool/Holder.hxx"
#include "util/Cancellable.hxx"

#include <boost/intrusive/list.hpp>

class WidgetResolver;

class WidgetResolverListener final
    : public boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
      PoolHolder,
      Cancellable {

    WidgetResolver &resolver;

    const WidgetResolverCallback callback;

#ifndef NDEBUG
    bool finished = false, aborted = false;
#endif

public:
    template<typename P>
    WidgetResolverListener(P &&_pool, WidgetResolver &_resolver,
                           WidgetResolverCallback _callback,
                           CancellablePointer &cancel_ptr) noexcept
        :PoolHolder(std::forward<P>(_pool)), resolver(_resolver),
         callback(_callback) {
        cancel_ptr = *this;
    }

    void Finish() noexcept;

private:
    void Destroy() noexcept {
        this->~WidgetResolverListener();
    }

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override;
};

class WidgetResolver {
    Widget &widget;

    boost::intrusive::list<WidgetResolverListener,
                           boost::intrusive::constant_time_size<false>> listeners;

    CancellablePointer cancel_ptr;

    bool finished = false;

#ifndef NDEBUG
    bool running = false;
    bool aborted = false;
#endif

public:
    explicit WidgetResolver(Widget &_widget)
        :widget(_widget) {}

    bool IsFinished() const noexcept {
        return finished;
    }

    void Start(TranslationService &service) {
        /* use the widget pool because the listener pool may be
           aborted, while the others still run */
        widget_class_lookup(widget.pool, widget.pool, service,
                            widget.class_name,
                            BIND_THIS_METHOD(RegistryCallback),
                            cancel_ptr);
    }

    void AddListener(WidgetResolverListener &listener) noexcept {
        assert(!finished);

        listeners.push_back(listener);
    }

    void RemoveListener(WidgetResolverListener &listener);

private:
    void Destroy() noexcept {
        this->~WidgetResolver();
    }

    void Abort();

    void RegistryCallback(const WidgetClass *cls) noexcept;
};

void
WidgetResolver::RemoveListener(WidgetResolverListener &listener)
{
    assert(widget.resolver == this);
    assert(!listeners.empty());
    assert(!finished || running);
    assert(!aborted);

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
    Destroy();
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

#ifndef NDEBUG
    aborted = true;
#endif

    resolver.RemoveListener(*this);

    Destroy();
}


/*
 * registry callback
 *
 */

inline void
WidgetResolverListener::Finish() noexcept
{
    assert(!finished);
    assert(!aborted);

#ifndef NDEBUG
    finished = true;
#endif

    callback();
    Destroy();
}

void
WidgetResolver::RegistryCallback(const WidgetClass *cls) noexcept
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

    Destroy();
}


/*
 * constructor
 *
 */

static WidgetResolver *
widget_resolver_alloc(Widget &widget) noexcept
{
    return widget.resolver = NewFromPool<WidgetResolver>(widget.pool, widget);
}

void
ResolveWidget(struct pool &pool,
              Widget &widget,
              TranslationService &service,
              WidgetResolverCallback callback,
              CancellablePointer &cancel_ptr) noexcept
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
    } else if (resolver->IsFinished()) {
        /* we have already failed to resolve this widget class; return
           immediately, don't try again */
        callback();
        return;
    }

    assert(pool_contains(widget.pool, widget.resolver,
                         sizeof(*widget.resolver)));

    /* add a new listener to the resolver */

    auto listener = NewFromPool<WidgetResolverListener>(pool, pool, *resolver,
                                                        callback,
                                                        cancel_ptr);

    resolver->AddListener(*listener);

    /* finally send request to the widget registry */

    if (is_new)
        resolver->Start(service);
}
