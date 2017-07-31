/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TranslationCache.hxx"
#include "http_server/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "util/StringView.hxx"

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
    /* the ones are: underscore, separator, underscore, null terminator */
    return 1 + host.size + 1 + 1 + listener_tag.size + 1;
}

/**
 * A helper class which generates key permutations for lookup.
 */
class LbTranslationCacheKeyIterator {
    static constexpr unsigned HOST = 0x1;
    static constexpr unsigned LISTENER_TAG = 0x2;

    const StringView host, listener_tag;

    std::unique_ptr<char[]> buffer;

    unsigned last = 4;

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
        if (last <= 0)
            return nullptr;

        last = NextIndex(last);
        assert(last < 4);
        return MakeKey(last);
    }

    /**
     * Generates a key for storing into the cache.
     */
    const char *FullKey() const {
        return MakeKey(HOST|LISTENER_TAG);
    }

private:
    static constexpr bool HasHost(unsigned i) {
        return i & HOST;
    }

    static constexpr bool HasListenerTag(unsigned i) {
        return i & LISTENER_TAG;
    }

    bool IsInactive(int i) const {
        assert(i < 4);

        return (HasHost(i) && host.IsNull()) ||
            (HasListenerTag(i) && listener_tag.IsNull());
    }

    unsigned NextIndex(unsigned i) const {
        assert(i <= 4);

        for (--i; IsInactive(i); --i) {}
        return i;
    }

    const char *MakeKey(unsigned i) const {
        assert(i < 4);

        char *result = buffer.get(), *p = result;

        if (HasHost(i)) {
            /* the underscore is just here to make a difference
               between "wildcard" (nothing) and "empty value"
               (underscore) */
            *p++ = '_';
            p = (char *)mempcpy(p, host.data, host.size);
        }

        *p++ = '|';

        if (HasListenerTag(i)) {
            /* see above for the underscore explanation */
            *p++ = '_';
            p = (char *)mempcpy(p, listener_tag.data, listener_tag.size);
        }

        *p = 0;
        return result;
    }
};

LbTranslationCache::Item::Item(const TranslateResponse &response)
    :status(response.status),
     https_only(response.https_only)
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
            logger(4, "hit '", key, "'");
            return item;
        }
    }

    logger(5, "miss");
    return nullptr;
}

void
LbTranslationCache::Put(const HttpServerRequest &request,
                        const char *listener_tag,
                        const TranslateResponse &response)
{
    if (response.max_age == std::chrono::seconds::zero())
        /* not cacheable */
        return;

    const Vary vary(response);

    if (!vary && !cache.IsEmpty()) {
        logger(4, "VARY disappeared, clearing cache");
        Clear();
    }

    seen_vary |= vary;

    LbTranslationCacheKeyIterator ki(vary, request, listener_tag);
    const char *key = ki.FullKey();

    logger(4, "store '", key, "'");

    cache.PutOrReplace(key, Item(response));
}
