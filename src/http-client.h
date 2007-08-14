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

#include <sys/types.h>
#include <event.h>

typedef struct http_client_connection *http_client_connection_t;

struct http_client_response {
    pool_t pool;
    http_client_connection_t connection;
    int status;
    strmap_t headers;
    off_t content_length;
    const struct http_client_request_handler *handler;
    void *handler_ctx;
};

struct http_client_request_handler {
    void (*response_body)(struct http_client_response *response,
                          const void *buffer, size_t length);
    void (*response_direct)(struct http_client_response *response, int fd);
    void (*free)(struct http_client_response *response);
};

typedef void (*http_client_callback_t)(struct http_client_response *response,
                                       void *ctx);

http_client_connection_t
http_client_connection_new(pool_t pool, int fd,
                           http_client_callback_t callback, void *ctx);

void
http_client_connection_close(http_client_connection_t connection);

void
http_client_request(http_client_connection_t connection,
                    http_method_t method, const char *uri);

void
http_client_response_direct_mode(http_client_connection_t connection);

void
http_client_response_finish(http_client_connection_t connection);

#endif
