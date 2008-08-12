/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_CLIENT_H
#define __BENG_HTTP_CLIENT_H

#include "pool.h"
#include "http.h"
#include "istream.h"

struct growing_buffer;
struct http_response_handler;
struct async_operation_ref;

struct http_client_connection_handler {
    void (*idle)(void *ctx);
    void (*free)(void *ctx);
};

struct http_client_connection *__attr_malloc
http_client_connection_new(pool_t pool, int fd,
                           const struct http_client_connection_handler *handler,
                           void *ctx);

void
http_client_connection_close(struct http_client_connection *connection);

void
http_client_connection_graceful(struct http_client_connection *connection);

void
http_client_request(struct http_client_connection *connection,
                    pool_t pool,
                    http_method_t method, const char *uri,
                    struct growing_buffer *headers,
                    istream_t body,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref);

#endif
