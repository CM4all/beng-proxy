/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CGI_H
#define __BENG_CGI_H

#include "istream.h"

#include <http/method.h>

struct strmap;
struct http_response_handler;
struct async_operation_ref;
struct jail_params;

G_GNUC_CONST
static inline GQuark
cgi_quark(void)
{
    return g_quark_from_static_string("cgi");
}

void
cgi_new(pool_t pool, const struct jail_params *jail,
        const char *interpreter, const char *action,
        const char *path,
        http_method_t method, const char *uri,
        const char *script_name, const char *path_info,
        const char *query_string,
        const char *document_root,
        const char *remote_addr,
        struct strmap *headers, istream_t body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref);

#endif
