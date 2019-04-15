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

#include "widget/Resolver.hxx"
#include "widget/Registry.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "pool/pool.hxx"
#include "pool/RootPool.hxx"
#include "util/Cancellable.hxx"
#include "util/Cast.hxx"

#include <assert.h>

static struct Context *global;

struct Context {
    RootPool root_pool;

    struct {
        CancellablePointer cancel_ptr;

        bool finished = false;

        /** abort in the callback? */
        bool abort = false;
    } first, second;

    struct Registry final : Cancellable {
        bool requested = false, finished = false, aborted = false;
        WidgetRegistryCallback callback = nullptr;

        /* virtual methods from class Cancellable */
        void Cancel() noexcept override {
            aborted = true;
        }
    } registry;

    Context() {
        global = this;
    }

    void ResolverCallback1() noexcept;
    void ResolverCallback2() noexcept;
};

const WidgetView *
widget_view_lookup(const WidgetView *view,
                   gcc_unused const char *name) noexcept
{
    return view;
}

void
Context::ResolverCallback1() noexcept
{
    assert(!first.finished);
    assert(!second.finished);

    first.finished = true;

    if (first.abort)
        second.cancel_ptr.Cancel();
}

void
Context::ResolverCallback2() noexcept
{
    assert(first.finished);
    assert(!second.finished);
    assert(!second.abort);

    second.finished = true;
}

/*
 * widget-registry.c emulation
 *
 */

void
widget_class_lookup(gcc_unused struct pool &pool,
                    gcc_unused struct pool &widget_pool,
                    gcc_unused struct tcache &translate_cache,
                    gcc_unused const char *widget_type,
                    WidgetRegistryCallback callback,
                    CancellablePointer &cancel_ptr)
{
    Context *data = global;
    assert(!data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(!data->registry.callback);

    data->registry.requested = true;
    data->registry.callback = callback;
    cancel_ptr = data->registry;
}

static void
widget_registry_finish(Context *data)
{
    assert(data->registry.requested);
    assert(!data->registry.finished);
    assert(!data->registry.aborted);
    assert(data->registry.callback);

    data->registry.finished = true;

    static const WidgetClass cls{};
    data->registry.callback(&cls);
}


/*
 * tests
 *
 */

static void
test_normal()
{
    Context data;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.cancel_ptr);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_abort()
{
    Context data;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.cancel_ptr);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    data.first.cancel_ptr.Cancel();

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_clients()
{
    Context data;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.cancel_ptr);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback2),
                  data.second.cancel_ptr);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}

static void
test_two_abort()
{
    Context data;
    data.first.abort = true;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto widget = NewFromPool<Widget>(*pool, *pool, nullptr);
    widget->class_name = "foo";

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback1),
                  data.first.cancel_ptr);

    ResolveWidget(*pool, *widget,
                  *(struct tcache *)(size_t)0x1,
                  BIND_METHOD(data, &Context::ResolverCallback2),
                  data.second.cancel_ptr);

    assert(!data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(!data.registry.finished);
    assert(!data.registry.aborted);

    widget_registry_finish(&data);

    assert(data.first.finished);
    assert(!data.second.finished);
    assert(data.registry.requested);
    assert(data.registry.finished);
    assert(!data.registry.aborted);

    pool_unref(pool);
    pool_commit();
}


/*
 * main
 *
 */

int
main(int, char **)
{
    /* run test suite */

    test_normal();
    test_abort();
    test_two_clients();
    test_two_abort();
}
