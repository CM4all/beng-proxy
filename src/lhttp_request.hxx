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
struct LhttpStock;
struct LhttpAddress;
struct http_response_handler;
struct async_operation_ref;
class HttpHeaders;

void
lhttp_request(struct pool &pool, LhttpStock &lhttp_stock,
              const LhttpAddress &address,
              http_method_t method,
              HttpHeaders &&headers, struct istream *body,
              const struct http_response_handler &handler, void *handler_ctx,
              struct async_operation_ref &async_ref);

#endif
