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

#pragma once

#include "Goto.hxx"
#include "translation/Stock.hxx"
#include "util/StringLess.hxx"
#include "util/Compiler.h"

#include <map>
#include <memory>

struct LbTranslationHandlerConfig;
class LbGotoMap;
struct HttpServerRequest;
struct TranslationInvalidateRequest;
struct TranslateResponse;
class EventLoop;
class CancellablePointer;
class LbTranslationCache;

class LbTranslationHandler final {
    const char *const name;

    TranslationStock stock;

    const std::map<const char *, LbGoto, StringLess> destinations;

    std::unique_ptr<LbTranslationCache> cache;

public:
    LbTranslationHandler(EventLoop &event_loop, LbGotoMap &goto_map,
                         const LbTranslationHandlerConfig &_config);
    ~LbTranslationHandler() noexcept;

    gcc_pure
    size_t GetAllocatedCacheMemory() const noexcept;

    void FlushCache();
    void InvalidateCache(const TranslationInvalidateRequest &request);

    const LbGoto *FindDestination(const char *destination_name) const {
        auto i = destinations.find(destination_name);
        return i != destinations.end()
            ? &i->second
            : nullptr;
    }

    void Pick(struct pool &pool, const HttpServerRequest &request,
              const char *listener_tag,
              const TranslateHandler &handler, void *ctx,
              CancellablePointer &cancel_ptr);

    void PutCache(const HttpServerRequest &request,
                  const char *listener_tag,
                  const TranslateResponse &response);
};
