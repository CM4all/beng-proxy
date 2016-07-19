/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "FilterResourceLoader.hxx"
#include "fcache.hxx"

#include <utility>

void
FilterResourceLoader::SendRequest(struct pool &pool,
                                  gcc_unused unsigned session_sticky,
                                  gcc_unused http_method_t method,
                                  const ResourceAddress &address,
                                  http_status_t status,
                                  StringMap &&headers,
                                  Istream *body,
                                  gcc_unused const char *body_etag,
                                  HttpResponseHandler &handler,
                                  CancellablePointer &cancel_ptr)
{
    assert(method == HTTP_METHOD_POST);

    filter_cache_request(cache, pool,
                         address, body_etag,
                         status, std::move(headers), body,
                         handler, cancel_ptr);
}
