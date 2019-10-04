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

#include "widget/Registry.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "http_address.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"
#include "pool/pool.hxx"
#include "pool/Ptr.hxx"
#include "PInstance.hxx"
#include "util/Cancellable.hxx"
#include "stopwatch.hxx"

#include <gtest/gtest.h>

#include <string.h>

class MyTranslationService final : public TranslationService, Cancellable {
public:
    bool aborted = false;

    /* virtual methods from class TranslationService */
    void SendRequest(struct pool &pool,
                     const TranslateRequest &request,
                     const StopwatchPtr &parent_stopwatch,
                     TranslateHandler &handler,
                     CancellablePointer &cancel_ptr) noexcept override;

    /* virtual methods from class Cancellable */
    void Cancel() noexcept override {
        aborted = true;
    }
};

struct Context : PInstance {
    bool got_class = false;
    const WidgetClass *cls = nullptr;

    void RegistryCallback(const WidgetClass *_cls) noexcept {
        got_class = true;
        cls = _cls;
    }
};

/*
 * tstock.c emulation
 *
 */

void
MyTranslationService::SendRequest(struct pool &pool,
                                  const TranslateRequest &request,
                                  const StopwatchPtr &,
                                  TranslateHandler &handler,
                                  CancellablePointer &cancel_ptr) noexcept
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
        response->views = NewFromPool<WidgetView>(pool, nullptr);
        response->views->address = {ShallowCopy(), response->address};
        handler.OnTranslateResponse(*response);
    } else if (strcmp(request.widget_type, "block") == 0) {
        cancel_ptr = *this;
    } else
        assert(0);
}


/*
 * tests
 *
 */

TEST(WidgetRegistry, Normal)
{
    MyTranslationService ts;
    Context data;
    WidgetRegistry registry(data.root_pool, ts);
    CancellablePointer cancel_ptr;

    auto pool = pool_new_linear(data.root_pool, "test", 8192);

    registry.LookupWidgetClass(pool, pool, "sync",
                               BIND_METHOD(data, &Context::RegistryCallback),
                               cancel_ptr);
    ASSERT_FALSE(ts.aborted);
    ASSERT_TRUE(data.got_class);
    ASSERT_NE(data.cls, nullptr);
    ASSERT_EQ(data.cls->views.address.type, ResourceAddress::Type::HTTP);
    ASSERT_STREQ(data.cls->views.address.GetHttp().host_and_port, "foo");
    ASSERT_STREQ(data.cls->views.address.GetHttp().path, "/");
    ASSERT_EQ(data.cls->views.next, nullptr);
    ASSERT_EQ(data.cls->views.transformation, nullptr);

    pool.reset();
    pool_commit();
}

/** caller aborts */
TEST(WidgetRegistry, Abort)
{
    MyTranslationService ts;
    Context data;
    WidgetRegistry registry(data.root_pool, ts);
    CancellablePointer cancel_ptr;

    auto pool = pool_new_linear(data.root_pool, "test", 8192);

    registry.LookupWidgetClass(pool, pool,  "block",
                               BIND_METHOD(data, &Context::RegistryCallback),
                               cancel_ptr);
    ASSERT_FALSE(data.got_class);
    ASSERT_FALSE(ts.aborted);

    cancel_ptr.Cancel();

    ASSERT_TRUE(ts.aborted);
    ASSERT_FALSE(data.got_class);

    pool.reset();
    pool_commit();
}
