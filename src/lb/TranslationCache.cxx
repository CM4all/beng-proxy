/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationCache.hxx"
#include "http_server/Request.hxx"
#include "translation/Response.hxx"
#include "beng-proxy/translation.h"

#include <daemon/log.h>

LbTranslationCache::Vary::Vary(const TranslateResponse &response)
    :host(response.VaryContains(TRANSLATE_HOST)) {}

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
    cache.Clear();
    seen_vary.Clear();
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
    if (seen_vary.host)
        return GetHost(request);
    else
        return std::string();
}

const LbTranslationCache::Item *
LbTranslationCache::Get(const HttpServerRequest &request,
                        const char *listener_tag)
{
    if (listener_tag != nullptr)
        /* ignore requests with listener_tag for now */
        return nullptr;

    auto key = GetKey(request);
    daemon_log(4, "[TranslationCache] hit '%s'\n", key.c_str());
    return cache.Get(key);
}

void
LbTranslationCache::Put(const HttpServerRequest &request,
                        const char *listener_tag,
                        const TranslateResponse &response)
{
    if (listener_tag != nullptr)
        /* ignore requests with listener_tag for now */
        return;

    const Vary vary(response);

    if (vary != seen_vary) {
        if (!cache.IsEmpty()) {
            daemon_log(4, "[TranslationCache] vary_host changed to %d, clearing cache\n",
                       vary.host);
            Clear();
        }

        seen_vary |= vary;
    }

    auto key = GetKey(request);
    daemon_log(4, "[TranslationCache] store '%s'\n", key.c_str());

    cache.PutOrReplace(std::move(key), Item(response));
}
