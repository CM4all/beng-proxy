#ifndef BENG_PROXY_HTTP_CACHE_RFC_HXX
#define BENG_PROXY_HTTP_CACHE_RFC_HXX

#include <inline/compiler.h>
#include <http/method.h>
#include <http/status.h>

#include <sys/types.h> /* for off_t */

struct http_cache_info *
http_cache_request_evaluate(struct pool *pool,
                            http_method_t method,
                            const struct resource_address *address,
                            const struct strmap *headers,
                            struct istream *body);

gcc_pure
bool
http_cache_vary_fits(const struct strmap &vary, const struct strmap *headers);

gcc_pure
bool
http_cache_vary_fits(const struct strmap *vary, const struct strmap *headers);

/**
 * Checks whether the specified cache item fits the current request.
 * This is not true if the Vary headers mismatch.
 */
bool
http_cache_document_fits(const struct http_cache_document *document,
                         const struct strmap *headers);

/**
 * Check whether the request should invalidate the existing cache.
 */
bool
http_cache_request_invalidate(http_method_t method);

/**
 * Check whether the HTTP response should be put into the cache.
 */
bool
http_cache_response_evaluate(struct http_cache_info *info,
                             http_status_t status, const struct strmap *headers,
                             off_t body_available);

/**
 * Copy all request headers mentioned in the Vary response header to a
 * new strmap.
 */
struct strmap *
http_cache_copy_vary(struct pool *pool, const char *vary,
                     const struct strmap *headers);

/**
 * The server sent us a non-"Not Modified" response.  Check if we want
 * to serve the cache item anyway, and discard the server's response.
 */
bool
http_cache_prefer_cached(const struct http_cache_document *document,
                         const struct strmap *response_headers);

#endif
