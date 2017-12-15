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

#include "widget/Registry.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "http_address.hxx"
#include "translation/Cache.hxx"
#include "translation/Stock.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"
#include "pool.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"

#include <string.h>

class TranslateStock final : public Cancellable {
public:
    bool aborted = false;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        aborted = true;
    }
};

struct Context : PInstance {
    bool got_class = false;
    const WidgetClass *cls = nullptr;

    void RegistryCallback(const WidgetClass *_cls) {
        got_class = true;
        cls = _cls;
    }
};

/*
 * tstock.c emulation
 *
 */

void
tstock_translate(gcc_unused TranslateStock &stock, struct pool &pool,
                 const TranslateRequest &request,
                 const TranslateHandler &handler, void *ctx,
                 CancellablePointer &cancel_ptr)
{
    assert(request.remote_host == NULL);
    assert(request.host == NULL);
    assert(request.uri == NULL);
    assert(request.widget_type != NULL);
    assert(request.session.IsNull());
    assert(request.param == NULL);

    if (strcmp(request.widget_type, "sync") == 0) {
        auto response = NewFromPool<TranslateResponse>(pool);
        response->address = *http_address_parse(pool, "http://foo/");
        response->views = NewFromPool<WidgetView>(pool);
        response->views->Init(nullptr);
        response->views->address = {ShallowCopy(), response->address};
        handler.response(*response, ctx);
    } else if (strcmp(request.widget_type, "block") == 0) {
        cancel_ptr = stock;
    } else
        assert(0);
}


/*
 * tests
 *
 */

/** normal run */
static void
test_normal()
{
    TranslateStock translate_stock;
    Context data;
    CancellablePointer cancel_ptr;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto *tcache = translate_cache_new(*pool, data.event_loop,
                                       translate_stock, 1024);

    widget_class_lookup(*pool, *pool, *tcache, "sync",
                        BIND_METHOD(data, &Context::RegistryCallback),
                        cancel_ptr);
    assert(!translate_stock.aborted);
    assert(data.got_class);
    assert(data.cls != NULL);
    assert(data.cls->views.address.type == ResourceAddress::Type::HTTP);
    assert(data.cls->views.address.GetHttp().protocol == HttpAddress::Protocol::HTTP);
    assert(strcmp(data.cls->views.address.GetHttp().host_and_port, "foo") == 0);
    assert(strcmp(data.cls->views.address.GetHttp().path, "/") == 0);
    assert(data.cls->views.next == NULL);
    assert(data.cls->views.transformation == NULL);

    pool_unref(pool);

    translate_cache_close(tcache);

    pool_commit();
}

/** caller aborts */
static void
test_abort()
{
    TranslateStock translate_stock;
    Context data;
    CancellablePointer cancel_ptr;

    auto *pool = pool_new_linear(data.root_pool, "test", 8192);

    auto *tcache = translate_cache_new(*pool, data.event_loop,
                                       translate_stock, 1024);

    widget_class_lookup(*pool, *pool, *tcache,  "block",
                        BIND_METHOD(data, &Context::RegistryCallback),
                        cancel_ptr);
    assert(!data.got_class);
    assert(!translate_stock.aborted);

    cancel_ptr.Cancel();

    /* need to unref the pool after aborted(), because our fake
       tstock_translate() implementation does not reference the
       pool */
    pool_unref(pool);

    assert(translate_stock.aborted);
    assert(!data.got_class);

    translate_cache_close(tcache);

    pool_commit();
}


/*
 * main
 *
 */

int
main(gcc_unused int argc, gcc_unused char **argv)
{
    test_normal();
    test_abort();
}
