/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_CACHE_HXX
#define BENG_LB_TRANSLATION_CACHE_HXX

#include "io/Logger.hxx"
#include "util/Cache.hxx"

#include <http/status.h>

#include <string>

struct HttpServerRequest;
struct TranslateRequest;
struct TranslateResponse;

class LbTranslationCache final {
    const LLogger logger;

public:
    struct Item {
        http_status_t status = http_status_t(0);
        uint16_t https_only = 0;
        std::string redirect, message, pool, canonical_host;

        explicit Item(const TranslateResponse &response);
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

    void Clear();
    void Invalidate(const TranslateRequest &request);

    const Item *Get(const HttpServerRequest &request,
                    const char *listener_tag);

    void Put(const HttpServerRequest &request,
             const char *listener_tag,
             const TranslateResponse &response);
};

#endif
