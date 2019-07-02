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

#ifndef BENG_LB_TRANSLATION_CACHE_HXX
#define BENG_LB_TRANSLATION_CACHE_HXX

#include "io/Logger.hxx"
#include "http/Status.h"
#include "util/Cache.hxx"

#include <string>

struct IncomingHttpRequest;
struct TranslationInvalidateRequest;
struct TranslateResponse;

class LbTranslationCache final {
    const LLogger logger;

public:
    struct Item {
        http_status_t status = http_status_t(0);
        uint16_t https_only = 0;
        std::string redirect, message, pool, canonical_host, site;

        explicit Item(const TranslateResponse &response);

        size_t GetAllocatedMemory() const noexcept {
            return sizeof(*this) + redirect.length() + message.length() +
                pool.length() + canonical_host.length() + site.length();
        }
    };

    struct Vary {
        bool host = false;
        bool listener_tag = false;

    public:
        Vary() = default;
        explicit Vary(const TranslateResponse &response);

        constexpr operator bool() const {
            return host || listener_tag;
        }

        void Clear() {
            host = false;
            listener_tag = false;
        }

        Vary &operator|=(const Vary other) {
            host |= other.host;
            listener_tag |= other.listener_tag;
            return *this;
        }
    };

private:
    typedef ::Cache<std::string, Item, 32768, 4093> Cache;
    Cache cache;

    Vary seen_vary;

public:
    LbTranslationCache()
        :logger("tcache") {}

    gcc_pure
    size_t GetAllocatedMemory() const noexcept;

    void Clear();
    void Invalidate(const TranslationInvalidateRequest &request);

    const Item *Get(const IncomingHttpRequest &request,
                    const char *listener_tag);

    void Put(const IncomingHttpRequest &request,
             const char *listener_tag,
             const TranslateResponse &response);
};

#endif
