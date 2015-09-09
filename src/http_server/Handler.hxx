/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_HTTP_SERVER_HANDLER_HXX
#define BENG_HTTP_SERVER_HANDLER_HXX

#include "glibfwd.hxx"

#include <http/status.h>

#include <sys/types.h>
#include <stdint.h>

struct http_server_request;
struct async_operation_ref;

struct http_server_connection_handler {
    void (*request)(struct http_server_request *request,
                    void *ctx,
                    struct async_operation_ref *async_ref);
    void (*log)(struct http_server_request *request,
                http_status_t status, off_t length,
                uint64_t bytes_received, uint64_t bytes_sent,
                void *ctx);

    /**
     * A fatal protocol level error has occurred, and the connection
     * was closed.
     *
     * This will be called instead of free().
     */
    void (*error)(GError *error, void *ctx);

    void (*free)(void *ctx);
};

#endif
