/*
 * Communicate with a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CGI_CLIENT_H
#define BENG_PROXY_CGI_CLIENT_H

#include <http/method.h>

struct pool;
struct stopwatch;
struct istream;
struct http_response_handler;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param input the stream received from the child process
 */
void
cgi_client_new(struct pool *pool, struct stopwatch *stopwatch,
               struct istream *input,
               const struct http_response_handler *handler, void *handler_ctx,
               struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
