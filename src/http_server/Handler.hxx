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

struct HttpServerConnectionHandler {
    void (*request)(struct http_server_request *request,
                    void *ctx,
                    struct async_operation_ref *async_ref);

    /**
     * @param length the number of response body (payload) bytes sent
     * to our HTTP client, or negative if there was no response body
     * (which is different from "empty response body")
     * @param bytes_received the number of raw bytes received from our
     * HTTP client
     * @param bytes_sent the number of raw bytes sent to our HTTP
     * client (which includes status line, headers and transport
     * encoding overhead such as chunk headers)
     */
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
