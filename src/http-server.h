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

/*#include <sys/socket.h>*/
#include <sys/types.h>
#include <event.h>

typedef struct http_server_connection *http_server_connection_t;

struct http_server_request {
    pool_t pool;
    http_server_connection_t connection;
    http_method_t method;
    char *uri;
    strmap_t headers;
    const struct http_server_request_handler *handler;
    void *handler_ctx;
};

struct http_server_request_handler {
    void (*request_body)(struct http_server_request *request,
                         const void *buffer, size_t length);
    size_t (*response_body)(struct http_server_request *request,
                            void *buffer, size_t max_length);
    void (*response_direct)(struct http_server_request *request, int fd);
    void (*free)(struct http_server_request *request);
};

/**
 * This callback is the application level request handler.  It is
 * called when the request line and the request headers have been
 * parsed.  It must install a request handler (request->handler).
 *
 * @param request the current request, or NULL if the connection was aborted
 * @param ctx the pointer which was passed along with this callback
 */
typedef void (*http_server_callback_t)(struct http_server_request *request,
                                       void *ctx);

http_server_connection_t
http_server_connection_new(pool_t pool, int fd,
                           http_server_callback_t callback, void *ctx);

void
http_server_connection_close(http_server_connection_t connection);

void
http_server_connection_free(http_server_connection_t *connection_r);

size_t
http_server_send(http_server_connection_t connection, void *p, size_t length);

size_t
http_server_send_status(http_server_connection_t connection, int status);

void
http_server_send_message(http_server_connection_t connection,
                         http_status_t status, const char *msg);

void
http_server_response_direct_mode(http_server_connection_t connection);

void
http_server_response_finish(http_server_connection_t connection);

#endif
