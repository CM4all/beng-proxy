/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CachedResourceLoader.hxx"
#include "http_cache.hxx"

#include <utility>

void
CachedResourceLoader::SendRequest(struct pool &pool,
                                  sticky_hash_t session_sticky,
                                  http_method_t method,
                                  const ResourceAddress &address,
                                  gcc_unused http_status_t status,
                                  StringMap &&headers,
                                  Istream *body,
                                  gcc_unused const char *body_etag,
                                  HttpResponseHandler &handler,
                                  CancellablePointer &cancel_ptr)
{
    http_cache_request(cache, pool, session_sticky,
                       method, address,
                       std::move(headers), body,
                       handler, cancel_ptr);
}
