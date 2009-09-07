/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_CLIENT_H
#define __BENG_AJP_CLIENT_H

#include "http.h"
#include "istream.h"

struct lease;
struct http_response_handler;
struct strmap;

void
ajp_client_request(pool_t pool, int fd,
                   const struct lease *lease, void *lease_ctx,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   struct strmap *headers,
                   istream_t body,
                   const struct http_response_handler *handler,
                   void *ctx,
                   struct async_operation_ref *async_ref);

#endif
