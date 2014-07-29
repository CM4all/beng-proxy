/*
 * High level "Local HTTP" client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_REQUEST_HXX
#define BENG_PROXY_LHTTP_REQUEST_HXX

#include <http/method.h>

struct pool;
struct istream;
struct lhttp_stock;
struct lhttp_address;
struct http_response_handler;
struct async_operation_ref;
class HttpHeaders;

void
lhttp_request(struct pool &pool, struct lhttp_stock &lhttp_stock,
              const struct lhttp_address &address,
              http_method_t method,
              HttpHeaders &&headers, struct istream *body,
              const struct http_response_handler &handler, void *handler_ctx,
              struct async_operation_ref &async_ref);

#endif
