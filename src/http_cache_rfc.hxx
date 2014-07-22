#ifndef BENG_PROXY_HTTP_CACHE_RFC_HXX
#define BENG_PROXY_HTTP_CACHE_RFC_HXX

#include <inline/compiler.h>
#include <http/method.h>
#include <http/status.h>

#include <sys/types.h> /* for off_t */

class Istream;
class StringMap;
struct ResourceAddress;
struct HttpCacheDocument;
struct HttpCacheRequestInfo;
struct HttpCacheResponseInfo;

bool
http_cache_request_evaluate(HttpCacheRequestInfo &info,
                            http_method_t method,
                            const ResourceAddress &address,
                            const StringMap &headers,
                            Istream *body);

gcc_pure
bool
http_cache_vary_fits(const StringMap &vary, const StringMap *headers);

gcc_pure
bool
http_cache_vary_fits(const StringMap *vary, const StringMap *headers);

/**
 * Check whether the request should invalidate the existing cache.
 */
bool
http_cache_request_invalidate(http_method_t method);

/**
 * Check whether the HTTP response should be put into the cache.
 */
bool
http_cache_response_evaluate(const HttpCacheRequestInfo &request_info,
                             HttpCacheResponseInfo &info,
                             http_status_t status, const StringMap &headers,
                             off_t body_available);

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
void
http_cache_copy_vary(StringMap &dest, struct pool &pool, const char *vary,
                     const StringMap &request_headers);

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
bool
http_cache_prefer_cached(const HttpCacheDocument &document,
                         const StringMap &response_headers);

#endif
