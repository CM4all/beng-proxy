/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationCache.hxx"
#include "http_server/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "util/StringView.hxx"

#include <daemon/log.h>

LbTranslationCache::Vary::Vary(const TranslateResponse &response)
    :host(response.VaryContains(TranslationCommand::HOST)),
     listener_tag(response.VaryContains(TranslationCommand::LISTENER_TAG)) {}

gcc_pure
static StringView
WithVary(const char *value, bool vary)
{
    if (!vary)
        return nullptr;

    if (value == nullptr)
        return "";

    return value;
}

gcc_pure
static size_t
CalculateKeyIteratorBufferSize(StringView host, StringView listener_tag)
{
    return host.size + 1 + listener_tag.size + 1;
}

/**
 * A helper class which generates key permutations for lookup.
 */
class LbTranslationCacheKeyIterator {
    const StringView host, listener_tag;

    std::unique_ptr<char[]> buffer;

    int last = 4;

public:
    LbTranslationCacheKeyIterator(LbTranslationCache::Vary vary,
                                  const HttpServerRequest &request,
                                  const char *_listener_tag)
        :host(vary.host
              ? WithVary(request.headers.Get("host"), vary.host)
              : nullptr),
         listener_tag(WithVary(_listener_tag, vary.listener_tag)),
         buffer(new char[CalculateKeyIteratorBufferSize(host, listener_tag)]) {}

    /**
     * Generates the next key.  Call this until it returns nullptr.
     */
    const char *NextKey() {
        assert(last >= 0);

        if (last <= 0)
            return nullptr;

        last = NextIndex(last);
        assert(last >= 0);
        assert(last < 4);
        return MakeKey(last);
    }

    /**
     * Generates a key for storing into the cache.
     */
    const char *FullKey() {
        /* 3 is the last index, with both bits set */
        return MakeKey(3);
    }

private:
    static constexpr bool HasHost(int i) {
        return i == 1 || i == 3;
    }

    static constexpr bool HasListenerTag(int i) {
        return i == 2 || i == 3;
    }

    bool IsInactive(int i) const {
        assert(i >= 0);
        assert(i < 4);

        return (HasHost(i) && host.IsNull()) ||
            (HasListenerTag(i) && listener_tag.IsNull());
    }

    int NextIndex(int i) const {
        assert(i > 0);
        assert(i <= 4);

        for (--i; IsInactive(i); --i) {}
        return i;
    }

    const char *MakeKey(int i) {
        assert(i >= 0);
        assert(i < 4);

        char *result = buffer.get(), *p = result;

        if (HasHost(i))
            p = (char *)mempcpy(p, host.data, host.size);

        *p++ = '|';

        if (HasListenerTag(i))
            p = (char *)mempcpy(p, listener_tag.data, listener_tag.size);

        *p = 0;
        return result;
    }
};

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

const LbTranslationCache::Item *
LbTranslationCache::Get(const HttpServerRequest &request,
                        const char *listener_tag)
{
    LbTranslationCacheKeyIterator ki(seen_vary, request, listener_tag);

    while (const char *key = ki.NextKey()) {
        const LbTranslationCache::Item *item = cache.Get(key);
        if (item != nullptr) {
            daemon_log(4, "[TranslationCache] hit '%s'\n", key);
            return item;
        }
    }

    daemon_log(5, "[TranslationCache] miss\n");
    return nullptr;
}

void
LbTranslationCache::Put(const HttpServerRequest &request,
                        const char *listener_tag,
                        const TranslateResponse &response)
{
    const Vary vary(response);

    if (!vary && !cache.IsEmpty()) {
        daemon_log(4, "[TranslationCache] VARY disappeared, clearing cache\n");
        Clear();
    }

    seen_vary |= vary;

    LbTranslationCacheKeyIterator ki(vary, request, listener_tag);
    const char *key = ki.FullKey();

    daemon_log(4, "[TranslationCache] store '%s'\n", key);

    cache.PutOrReplace(key, Item(response));
}
