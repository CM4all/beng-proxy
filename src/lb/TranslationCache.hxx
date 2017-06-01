/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_LB_TRANSLATION_CACHE_HXX
#define BENG_LB_TRANSLATION_CACHE_HXX

#include "util/Cache.hxx"

#include <http/status.h>

#include <string>
#include <memory>

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
    std::unique_ptr<Cache> cache;
    std::unique_ptr<Item> one_item;

public:
    const Item *Get(const HttpServerRequest &request);
    void Put(const HttpServerRequest &request,
             const TranslateResponse &response);
};

#endif
