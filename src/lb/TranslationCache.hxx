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

private:
    typedef ::Cache<std::string, Item, 32768, 4093> Cache;
    Cache cache;

    bool seen_vary_host = false;

public:
    void Clear();

    const Item *Get(const HttpServerRequest &request);
    void Put(const HttpServerRequest &request,
             const TranslateResponse &response);

private:
    std::string GetKey(const HttpServerRequest &request) const noexcept;
};

#endif
