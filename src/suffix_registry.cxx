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

#include "suffix_registry.hxx"
#include "translation/Cache.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "widget/View.hxx"
#include "pool/pool.hxx"

struct SuffixRegistryLookup {
    TranslateRequest request;

    SuffixRegistryHandler &handler;

    SuffixRegistryLookup(ConstBuffer<void> payload,
                         const char *suffix,
                         SuffixRegistryHandler &_handler)
        :handler(_handler) {
        request.Clear();
        request.content_type_lookup = payload;
        request.suffix = suffix;
    }
};

/*
 * TranslateHandler
 *
 */

static void
suffix_translate_response(TranslateResponse &response, void *ctx)
{
    SuffixRegistryLookup &lookup = *(SuffixRegistryLookup *)ctx;

    lookup.handler.OnSuffixRegistrySuccess(response.content_type,
                                           response.views != nullptr
                                           ? response.views->transformation
                                           : nullptr);
}

static void
suffix_translate_error(std::exception_ptr ep, void *ctx)
{
    SuffixRegistryLookup &lookup = *(SuffixRegistryLookup *)ctx;

    lookup.handler.OnSuffixRegistryError(ep);
}

static constexpr TranslateHandler suffix_translate_handler = {
    .response = suffix_translate_response,
    .error = suffix_translate_error,
};

/*
 * constructor
 *
 */

void
suffix_registry_lookup(struct pool &pool,
                       struct tcache &tcache,
                       ConstBuffer<void> payload,
                       const char *suffix,
                       SuffixRegistryHandler &handler,
                       CancellablePointer &cancel_ptr)
{
    auto lookup = NewFromPool<SuffixRegistryLookup>(pool,
                                                    payload, suffix,
                                                    handler);

    translate_cache(pool, tcache, lookup->request,
                    suffix_translate_handler, lookup, cancel_ptr);
}
