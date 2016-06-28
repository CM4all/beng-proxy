/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CachedResourceLoader.hxx"
#include "http_cache.hxx"

#include <utility>

void
CachedResourceLoader::SendRequest(struct pool &pool,
                                  unsigned session_sticky,
                                  http_method_t method,
                                  const ResourceAddress &address,
                                  gcc_unused http_status_t status,
                                  StringMap &&headers,
                                  Istream *body,
                                  gcc_unused const char *body_etag,
                                  const struct http_response_handler &handler,
                                  void *handler_ctx,
                                  struct async_operation_ref &async_ref)
{
    http_cache_request(cache, pool, session_sticky,
                       method, address,
                       std::move(headers), body,
                       handler, handler_ctx, async_ref);
}
