/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

#include "pool.h"
#include "strmap.h"

/*#include <sys/socket.h>*/
#include <sys/types.h>
#include <event.h>

typedef enum {
    HTTP_METHOD_NULL = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_INVALID,
} http_method_t;

typedef enum {
    HTTP_STATUS_OK = 200,
} http_status_t;

typedef struct http_server_connection *http_server_connection_t;

struct http_server_request {
    pool_t pool;
    http_server_connection_t connection;
    http_method_t method;
    char *uri;
    strmap_t headers;
};

typedef void (*http_server_callback_t)(struct http_server_request *request,
                                       /*const void *body, size_t body_length,*/
                                       void *ctx);

int
http_server_connection_new(pool_t pool, int fd,
                           http_server_callback_t callback, void *ctx,
                           http_server_connection_t *connection_r);

void
http_server_connection_free(http_server_connection_t *connection_r);

void
http_server_send_message(http_server_connection_t connection,
                         http_status_t status, const char *msg);

void
http_server_response_finish(http_server_connection_t connection);

#endif
