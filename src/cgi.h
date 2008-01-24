/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CGI_H
#define __BENG_CGI_H

#include "istream.h"
#include "http.h"

struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
cgi_new(pool_t pool,
        const char *path,
        http_method_t method, const char *uri,
        struct strmap *headers, istream_t body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref);

#endif
