/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_CLIENT_H
#define __BENG_FCGI_CLIENT_H

#include "istream.h"
#include "http.h"

struct lease;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
fcgi_client_request(pool_t pool, int fd,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    struct strmap *headers, istream_t body,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
