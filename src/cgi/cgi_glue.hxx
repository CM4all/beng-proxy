/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_GLUE_HXX
#define BENG_PROXY_CGI_GLUE_HXX

#include <http/method.h>

struct pool;
struct CgiAddress;
class EventLoop;
class Istream;
class SpawnService;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
cgi_new(SpawnService &spawn_service, EventLoop &event_loop,
        struct pool *pool, http_method_t method,
        const CgiAddress *address,
        const char *remote_addr,
        struct strmap *headers, Istream *body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref);

#endif
