/*
 * High level HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_REQUEST_HXX
#define BENG_PROXY_HTTP_REQUEST_HXX

#include <http/method.h>

struct pool;
struct istream;
struct TcpBalancer;
struct SocketFilter;
class SocketFilterFactory;
struct HttpAddress;
struct http_response_handler;
struct async_operation_ref;
class HttpHeaders;

/**
 * @param session_sticky a portion of the session id that is used to
 * select the worker; 0 means disable stickiness
 */
void
http_request(struct pool &pool,
             TcpBalancer &tcp_balancer,
             unsigned session_sticky,
             const SocketFilter *filter, SocketFilterFactory *filter_factory,
             http_method_t method,
             const HttpAddress &address,
             HttpHeaders &&headers, struct istream *body,
             const struct http_response_handler &handler,
             void *handler_ctx,
             struct async_operation_ref &async_ref);

#endif
