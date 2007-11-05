/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

#include "pool.h"
#include "strmap.h"
#include "http.h"
#include "istream.h"
#include "growing-buffer.h"

#include <stddef.h>

typedef struct http_server_connection *http_server_connection_t;

struct http_server_request {
    pool_t pool;
    http_server_connection_t connection;
    const char *remote_host;

    /* request metadata */
    http_method_t method;
    char *uri;
    strmap_t headers;

    off_t content_length;
    istream_t body;
};

struct http_server_connection_handler {
    void (*request)(struct http_server_request *request,
                    void *ctx);
    void (*free)(void *ctx);
};


void
http_server_connection_new(pool_t pool, int fd,
                           const char *remote_host,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           http_server_connection_t *connection_r);

void
http_server_connection_free(http_server_connection_t *connection_r);

void
http_server_response(struct http_server_request *request,
                     http_status_t status,
                     growing_buffer_t headers,
                     off_t content_length, istream_t body);

void
http_server_send_message(struct http_server_request *request,
                         http_status_t status, const char *msg);

void
http_server_send_redirect(struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg);

#endif
