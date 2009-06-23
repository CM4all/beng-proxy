/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_SERVER_H
#define __BENG_HTTP_SERVER_H

#include "pool.h"
#include "http.h"
#include "istream.h"

struct sockaddr;
struct growing_buffer;
struct async_operation_ref;

struct http_server_connection;

struct http_server_request {
    pool_t pool;
    struct http_server_connection *connection;

    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *remote_host;

    /* request metadata */
    http_method_t method;
    char *uri;
    struct strmap *headers;

    istream_t body;
};

struct http_server_connection_handler {
    void (*request)(struct http_server_request *request,
                    void *ctx,
                    struct async_operation_ref *async_ref);
    void (*log)(struct http_server_request *request,
                http_status_t status, off_t length,
                void *ctx);
    void (*free)(void *ctx);
};


void
http_server_connection_new(pool_t pool, int fd,
                           const struct sockaddr *local_address,
                           size_t local_address_length,
                           const char *remote_host,
                           const struct http_server_connection_handler *handler,
                           void *ctx,
                           struct http_server_connection **connection_r);

void
http_server_connection_close(struct http_server_connection *connection);

void
http_server_connection_graceful(struct http_server_connection *connection);

static inline bool
http_server_request_has_body(const struct http_server_request *request)
{
    return request->body != NULL;
}

void
http_server_response(const struct http_server_request *request,
                     http_status_t status,
                     struct growing_buffer *headers,
                     istream_t body);

void
http_server_send_message(const struct http_server_request *request,
                         http_status_t status, const char *msg);

void
http_server_send_redirect(const struct http_server_request *request,
                          http_status_t status, const char *location,
                          const char *msg);

#endif
