/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationCache.hxx"
#include "http_server/Request.hxx"
#include "translation/Response.hxx"
#include "beng-proxy/translation.h"

#include <daemon/log.h>

LbTranslationCache::Item::Item(const TranslateResponse &response)
    :status(response.status)
{
    if (response.redirect != nullptr)
        redirect = response.redirect;

    if (response.message != nullptr)
        message = response.message;

    if (response.pool != nullptr)
        pool = response.pool;

    if (response.canonical_host != nullptr)
        canonical_host = response.canonical_host;
}

void
LbTranslationCache::Clear()
{
    cache.reset();
    one_item.reset();
}

static std::string
GetHost(const HttpServerRequest &request)
{
    std::string result;

    const char *host = request.headers.Get("host");
    if (host != nullptr)
        result = host;

    return result;
}

const LbTranslationCache::Item *
LbTranslationCache::Get(const HttpServerRequest &request)
{
    if (cache) {
        auto host = GetHost(request);
        daemon_log(4, "[TranslationCache] hit %s\n", host.c_str());
        return cache->Get(host);
    } else if (one_item) {
        daemon_log(4, "[TranslationCache] hit\n");
        return one_item.get();
    } else
        return nullptr;
}

void
LbTranslationCache::Put(const HttpServerRequest &request,
                        const TranslateResponse &response)
{
    bool vary_host = response.VaryContains(TRANSLATE_HOST);
    if (vary_host) {
        if (one_item) {
            daemon_log(4, "[TranslationCache] vary_host appeared, clearing cache\n");
            one_item.reset();
        }

        if (!cache)
            cache.reset(new Cache());

        auto host = GetHost(request);
        daemon_log(4, "[TranslationCache] store %s\n", host.c_str());

        // TODO: replace existing item and eliminate these two calls
        if (cache->Get(host) != nullptr)
            cache->Remove(host);

        cache->Put(std::move(host), Item(response));
    } else {
        if (cache) {
            daemon_log(4, "[TranslationCache] vary_host disappeared, clearing cache\n");
            cache.reset();
        }

        daemon_log(4, "[TranslationCache] store\n");
        one_item.reset(new Item(response));
    }
}
