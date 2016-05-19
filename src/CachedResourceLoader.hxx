/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CACHED_RESOURCE_LOADER_HXX
#define BENG_PROXY_CACHED_RESOURCE_LOADER_HXX

#include "ResourceLoader.hxx"

class HttpCache;

/**
 * A #ResourceLoader implementation which sends HTTP requests through
 * the HTTP cache.
 */
class CachedResourceLoader final : public ResourceLoader {
    HttpCache &cache;

public:
    explicit CachedResourceLoader(HttpCache &_cache)
        :cache(_cache) {}

    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     unsigned session_sticky,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, struct strmap *headers,
                     Istream *body, const char *body_etag,
                     const struct http_response_handler &handler,
                     void *handler_ctx,
                     struct async_operation_ref &async_ref) override;
};

#endif
