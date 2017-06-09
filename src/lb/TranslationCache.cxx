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
    seen_vary_host = false;
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

inline std::string
LbTranslationCache::GetKey(const HttpServerRequest &request) const noexcept
{
    if (seen_vary_host)
        return GetHost(request);
    else
        return std::string();
}

const LbTranslationCache::Item *
LbTranslationCache::Get(const HttpServerRequest &request)
{
    if (cache) {
        auto key = GetKey(request);
        daemon_log(4, "[TranslationCache] hit '%s'\n", key.c_str());
        return cache->Get(key);
    } else
        return nullptr;
}

void
LbTranslationCache::Put(const HttpServerRequest &request,
                        const TranslateResponse &response)
{
    bool vary_host = response.VaryContains(TRANSLATE_HOST);
    if (vary_host != seen_vary_host) {
        if (cache && !cache->IsEmpty()) {
            daemon_log(4, "[TranslationCache] vary_host changed to %d, clearing cache\n",
                       vary_host);
            Clear();
        }

        seen_vary_host = vary_host;
    }

    if (!cache)
        cache.reset(new Cache());

    auto key = GetKey(request);
    daemon_log(4, "[TranslationCache] store '%s'\n", key.c_str());

    cache->PutOrReplace(std::move(key), Item(response));
}
