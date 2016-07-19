/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FILTER_RESOURCE_LOADER_HXX
#define BENG_PROXY_FILTER_RESOURCE_LOADER_HXX

#include "ResourceLoader.hxx"

class FilterCache;

/**
 * A #ResourceLoader implementation which sends HTTP requests through
 * the filter cache.
 */
class FilterResourceLoader final : public ResourceLoader {
    FilterCache &cache;

public:
    explicit FilterResourceLoader(FilterCache &_cache)
        :cache(_cache) {}

    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     unsigned session_sticky,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};

#endif
