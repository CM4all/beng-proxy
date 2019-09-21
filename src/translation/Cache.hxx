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

#include "Service.hxx"
#include "util/Compiler.h"

#include <memory>

#include <stdint.h>

enum class TranslationCommand : uint16_t;
class EventLoop;
struct AllocatorStats;
template<typename T> struct ConstBuffer;

struct tcache;

/**
 * Cache for translation server responses.
 */
class TranslationCache final : public TranslationService {
    std::unique_ptr<struct tcache> cache;

public:
    /**
     * @param handshake_cacheable if false, then all requests are
     * deemed uncacheable until the first response is received
     */
    TranslationCache(struct pool &pool, EventLoop &event_loop,
                     TranslationService &next,
                     unsigned max_size, bool handshake_cacheable=true);

    ~TranslationCache() noexcept;

    void ForkCow(bool inherit) noexcept;

    gcc_pure
    AllocatorStats GetStats() const noexcept;

    /**
     * Flush all items from the cache.
     */
    void Flush() noexcept;

    /**
     * Flush selected items from the cache.
     *
     * @param request a request with parameters to compare with
     * @param vary a list of #beng_translation_command codes which define
     * the cache item filter
     */
    void Invalidate(const TranslateRequest &request,
                    ConstBuffer<TranslationCommand> vary,
                    const char *site) noexcept;

    /* virtual methods from class TranslationService */
    void SendRequest(struct pool &pool,
                     const TranslateRequest &request,
                     const StopwatchPtr &parent_stopwatch,
                     const TranslateHandler &handler, void *ctx,
                     CancellablePointer &cancel_ptr) noexcept override;
};
