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

#include "Registry.hxx"
#include "Class.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Transformation.hxx"
#include "AllocatorPtr.hxx"
#include "io/Logger.hxx"
#include "util/Exception.hxx"
#include "stopwatch.hxx"

static void
widget_registry_lookup(struct pool &pool,
                       TranslationService &service,
                       const char *widget_type,
                       TranslateHandler &handler,
                       CancellablePointer &cancel_ptr) noexcept
{
    auto request = NewFromPool<TranslateRequest>(pool);

    request->widget_type = widget_type;

    service.SendRequest(pool, *request,
                        nullptr, // TODO
                        handler, cancel_ptr);
}

struct WidgetRegistryLookup final : TranslateHandler {
    struct pool &pool;

    const WidgetRegistryCallback callback;

    WidgetRegistryLookup(struct pool &_pool,
                         WidgetRegistryCallback _callback) noexcept
        :pool(_pool), callback(_callback) {}

    /* virtual methods from TranslateHandler */
    void OnTranslateResponse(TranslateResponse &response) noexcept override;
    void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
WidgetRegistryLookup::OnTranslateResponse(TranslateResponse &response) noexcept
{
    assert(response.views != nullptr);

    if (response.status != 0) {
        callback(nullptr);
        return;
    }

    auto cls = NewFromPool<WidgetClass>(pool);
    cls->local_uri = response.local_uri;
    cls->untrusted_host = response.untrusted;
    cls->untrusted_prefix = response.untrusted_prefix;
    cls->untrusted_site_suffix = response.untrusted_site_suffix;
    cls->untrusted_raw_site_suffix = response.untrusted_raw_site_suffix;
    if (cls->untrusted_host == nullptr)
        /* compatibility with v0.7.16 */
        cls->untrusted_host = response.host;
    cls->cookie_host = response.cookie_host;
    cls->group = response.widget_group;
    cls->container_groups = std::move(response.container_groups);
    cls->direct_addressing = response.direct_addressing;
    cls->stateful = response.stateful;
    cls->require_csrf_token = response.require_csrf_token;
    cls->anchor_absolute = response.anchor_absolute;
    cls->info_headers = response.widget_info;
    cls->dump_headers = response.dump_headers;
    cls->views.CopyChainFrom(pool, *response.views);

    callback(cls);
}

void
WidgetRegistryLookup::OnTranslateError(std::exception_ptr ep) noexcept
{
    LogConcat(2, "WidgetRegistry", ep);

    callback(nullptr);
}

void
widget_class_lookup(struct pool &pool, struct pool &widget_pool,
                    TranslationService &service,
                    const char *widget_type,
                    WidgetRegistryCallback callback,
                    CancellablePointer &cancel_ptr) noexcept
{
    assert(widget_type != nullptr);

    auto lookup = NewFromPool<WidgetRegistryLookup>(pool, widget_pool,
                                                    callback);
    widget_registry_lookup(pool, service, widget_type,
                           *lookup, cancel_ptr);
}
