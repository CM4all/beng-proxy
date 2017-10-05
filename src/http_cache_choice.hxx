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

/*
 * Caching HTTP responses.  Memcached indirect backend.
 */

#ifndef BENG_PROXY_HTTP_CACHE_CHOICE_HXX
#define BENG_PROXY_HTTP_CACHE_CHOICE_HXX

#include "strmap.hxx"
#include "util/Compiler.h"

#include <chrono>
#include <exception>

struct pool;
struct HttpCacheChoice;
struct HttpCacheResponseInfo;
class StringMap;
struct MemachedStock;
class CancellablePointer;

struct HttpCacheChoiceInfo {
    std::chrono::system_clock::time_point expires;
    StringMap vary;

    explicit HttpCacheChoiceInfo(struct pool &p)
        :vary(p) {}

    gcc_pure
    bool VaryFits(const StringMap *headers) const;
};

typedef void (*http_cache_choice_get_t)(const char *key, bool unclean,
                                        std::exception_ptr ep, void *ctx);
typedef void (*http_cache_choice_commit_t)(std::exception_ptr ep, void *ctx);
typedef bool (*http_cache_choice_filter_t)(const HttpCacheChoiceInfo *info,
                                           std::exception_ptr ep, void *ctx);
typedef void (*http_cache_choice_cleanup_t)(std::exception_ptr ep, void *ctx);
typedef void (*http_cache_choice_delete_t)(std::exception_ptr ep, void *ctx);

const char *
http_cache_choice_vary_key(struct pool &pool, const char *uri,
                           const StringMap *vary);

void
http_cache_choice_get(struct pool &pool, MemachedStock &stock,
                      const char *uri, const StringMap *request_headers,
                      http_cache_choice_get_t callback,
                      void *callback_ctx,
                      CancellablePointer &cancel_ptr);

HttpCacheChoice *
http_cache_choice_prepare(struct pool &pool, const char *uri,
                          const HttpCacheResponseInfo &info,
                          const StringMap &vary);

void
http_cache_choice_commit(HttpCacheChoice &choice,
                         MemachedStock &stock,
                         http_cache_choice_commit_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr);

/**
 * Filter the choice record, keep only items accepted by the filter
 * function.  After the last document, the filter function is called
 * with document=nullptr.
 */
void
http_cache_choice_filter(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_filter_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr);

/**
 * Clean up the choice record, removing expired items.
 */
void
http_cache_choice_cleanup(struct pool &pool, MemachedStock &stock,
                          const char *uri,
                          http_cache_choice_cleanup_t callback,
                          void *callback_ctx,
                          CancellablePointer &cancel_ptr);

/**
 * Deletes the choice record.
 *
 * The data records are not deleted, but since no pointer exists
 * anymore, they are unused.  We could optimize later by deleting
 * those, too.
 */
void
http_cache_choice_delete(struct pool &pool, MemachedStock &stock,
                         const char *uri,
                         http_cache_choice_delete_t callback,
                         void *callback_ctx,
                         CancellablePointer &cancel_ptr);

#endif
