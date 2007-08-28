/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_CLIENT_H
#define __BENG_HTTP_CLIENT_H

#include "pool.h"
#include "strmap.h"
#include "http.h"
#include "istream.h"
#include "growing-buffer.h"

#include <stddef.h>

typedef struct http_client_connection *http_client_connection_t;

struct http_client_connection_handler {
    void (*idle)(void *ctx);
    void (*free)(void *ctx);
};

struct http_client_response_handler {
    void (*response)(http_status_t status, strmap_t headers,
                     off_t content_length, istream_t body,
                     void *ctx);
    void (*free)(void *ctx);
};

http_client_connection_t attr_malloc
http_client_connection_new(pool_t pool, int fd,
                           const struct http_client_connection_handler *handler,
                           void *ctx);

void
http_client_connection_close(http_client_connection_t connection);

void
http_client_request(http_client_connection_t connection,
                    http_method_t method, const char *uri,
                    growing_buffer_t headers,
                    const struct http_client_response_handler *handler,
                    void *ctx);

#endif
