/*
 * High level "Local HTTP" client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LHTTP_REQUEST_HXX
#define BENG_PROXY_LHTTP_REQUEST_HXX

#include <http/method.h>

struct pool;
class EventLoop;
class Istream;
class LhttpStock;
struct LhttpAddress;
class HttpResponseHandler;
class CancellablePointer;
class HttpHeaders;

void
lhttp_request(struct pool &pool, EventLoop &event_loop,
              LhttpStock &lhttp_stock,
              const LhttpAddress &address,
              http_method_t method,
              HttpHeaders &&headers, Istream *body,
              HttpResponseHandler &handler,
              CancellablePointer &cancel_ptr);

#endif
