/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_CLIENT_H
#define __BENG_AJP_CLIENT_H

#include "http.h"
#include "istream.h"

struct ajp_connection;
struct http_client_connection_handler;
struct http_response_handler;
struct strmap;

struct ajp_connection * __attr_malloc
ajp_new(pool_t pool, int fd,
        const struct http_client_connection_handler *handler, void *ctx);

void
ajp_connection_close(struct ajp_connection *connection);

void
ajp_request(struct ajp_connection *connection, pool_t pool,
            http_method_t method, const char *uri,
            struct strmap *headers,
            istream_t body,
            const struct http_response_handler *handler,
            void *ctx,
            struct async_operation_ref *async_ref);

#endif
