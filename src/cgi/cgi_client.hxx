/*
 * Communicate with a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_CLIENT_HXX
#define BENG_PROXY_CGI_CLIENT_HXX

#include <http/method.h>

struct pool;
struct stopwatch;
class Istream;
struct http_response_handler;
struct async_operation_ref;

/**
 * @param input the stream received from the child process
 */
void
cgi_client_new(struct pool &pool, struct stopwatch *stopwatch,
               Istream &input,
               const struct http_response_handler &handler, void *handler_ctx,
               struct async_operation_ref &async_ref);

#endif
