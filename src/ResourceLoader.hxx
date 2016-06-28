/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RESOURCE_LOADER_HXX
#define BENG_PROXY_RESOURCE_LOADER_HXX

#include <http/method.h>
#include <http/status.h>

struct pool;
class Istream;
struct ResourceAddress;
class StringMap;
struct http_response_handler;
struct async_operation_ref;

/**
 * Load resources specified by a resource_address.
 */
class ResourceLoader {
public:
    /**
     * Requests a resource.
     *
     * @param session_sticky a portion of the session id that is used to
     * select the worker; 0 means disable stickiness
     * @param address the address of the resource
     * @param status a HTTP status code for protocols which do have one
     * @param body the request body
     * @param body_etag a unique identifier for the request body; if
     * not nullptr, it may be used to cache POST requests
     */
    virtual void SendRequest(struct pool &pool,
                             unsigned session_sticky,
                             http_method_t method,
                             const ResourceAddress &address,
                             http_status_t status, StringMap &&headers,
                             Istream *body, const char *body_etag,
                             const struct http_response_handler &handler,
                             void *handler_ctx,
                             struct async_operation_ref &async_ref) = 0;
};

#endif
