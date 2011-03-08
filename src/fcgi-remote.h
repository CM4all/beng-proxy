/*
 * High level FastCGI client for remote FastCGI servers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_REMOTE_H
#define BENG_PROXY_FCGI_REMOTE_H

#include "istream.h"

#include <http/method.h>

struct hstock;
struct address_list;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

void
fcgi_remote_request(pool_t pool, struct hstock *tcp_stock,
                    const struct address_list *address_list,
                    const char *path,
                    http_method_t method, const char *uri,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    const char *remote_addr,
                    struct strmap *headers, istream_t body,
                    const char *const params[], unsigned num_params,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
