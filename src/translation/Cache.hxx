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

#ifndef BENG_PROXY_TCACHE_HXX
#define BENG_PROXY_TCACHE_HXX

#include "util/Compiler.h"

#include <stdint.h>

enum class TranslationCommand : uint16_t;
struct pool;
class EventLoop;
class TranslateStock;
struct TranslateHandler;
struct TranslateRequest;
class CancellablePointer;
struct AllocatorStats;
template<typename T> struct ConstBuffer;

/**
 * Cache for translation server responses.
 */
struct tcache;

/**
 * @param handshake_cacheable if false, then all requests are deemed
 * uncacheable until the first response is received
 */
struct tcache *
translate_cache_new(struct pool &pool, EventLoop &event_loop,
                    TranslateStock &stock,
                    unsigned max_size, bool handshake_cacheable=true);

void
translate_cache_close(struct tcache *tcache);

void
translate_cache_fork_cow(struct tcache &cache, bool inherit);

gcc_pure
AllocatorStats
translate_cache_get_stats(const struct tcache &tcache);

/**
 * Flush all items from the cache.
 */
void
translate_cache_flush(struct tcache &tcache);

/**
 * Query an item from the cache.  If not present, the request is
 * forwarded to the "real" translation server, and its response is
 * added to the cache.
 */
void
translate_cache(struct pool &pool, struct tcache &tcache,
                const TranslateRequest &request,
                const TranslateHandler &handler, void *ctx,
                CancellablePointer &cancel_ptr);

/**
 * Flush selected items from the cache.
 *
 * @param request a request with parameters to compare with
 * @param vary a list of #beng_translation_command codes which define
 * the cache item filter
 */
void
translate_cache_invalidate(struct tcache &tcache,
                           const TranslateRequest &request,
                           ConstBuffer<TranslationCommand> vary,
                           const char *site);

#endif
