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

/*#include <sys/socket.h>*/
#include <sys/types.h>
#include <event.h>

typedef struct http_server_connection *http_server_connection_t;

struct http_server_request {
    pool_t pool;
    http_server_connection_t connection;

    /* request metadata */
    http_method_t method;
    char *uri;
    strmap_t headers;
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

http_server_connection_t attr_malloc
http_server_connection_new(pool_t pool, int fd,
                           http_server_callback_t callback, void *ctx);

void
http_server_connection_close(http_server_connection_t connection);

void
http_server_connection_free(http_server_connection_t *connection_r);

size_t
http_server_send_status(http_server_connection_t connection, int status);

void
http_server_try_write(http_server_connection_t connection);

void
http_server_response(struct http_server_request *request,
                     http_status_t status, strmap_t headers,
                     off_t content_length, istream_t body);

void
http_server_send_message(struct http_server_request *request,
                         http_status_t status, const char *msg);

#endif
