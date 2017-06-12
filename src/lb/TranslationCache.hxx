/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_CACHE_HXX
#define BENG_LB_TRANSLATION_CACHE_HXX

#include "util/Cache.hxx"

#include <http/status.h>

#include <string>

struct HttpServerRequest;
struct TranslateResponse;

class LbTranslationCache final {
public:
    struct Item {
        http_status_t status = http_status_t(0);
        std::string redirect, message, pool, canonical_host;

        explicit Item(const TranslateResponse &response);
    };

    struct Vary {
        bool host = false;

    public:
        Vary() = default;
        explicit Vary(const TranslateResponse &response);

        constexpr operator bool() const {
            return host;
        }

        void Clear() {
            host = false;
        }

        Vary &operator|=(const Vary other) {
            host |= other.host;
            return *this;
        }
    };

private:
    typedef ::Cache<std::string, Item, 32768, 4093> Cache;
    Cache cache;

    Vary seen_vary;

public:
    void Clear();

    const Item *Get(const HttpServerRequest &request,
                    const char *listener_tag);

    void Put(const HttpServerRequest &request,
             const char *listener_tag,
             const TranslateResponse &response);

private:
    std::string GetKey(const HttpServerRequest &request) const noexcept;
};

#endif
