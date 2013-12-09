/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CGI_H
#define __BENG_CGI_H

#include <http/method.h>

struct pool;
struct cgi_address;
struct istream;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
cgi_new(struct pool *pool, http_method_t method,
        const struct cgi_address *address,
        const char *remote_addr,
        struct strmap *headers, struct istream *body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref);

#endif
