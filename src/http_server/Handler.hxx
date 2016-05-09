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

class HttpServerConnectionHandler {
public:
    virtual void HandleHttpRequest(struct http_server_request &request,
                                   struct async_operation_ref &async_ref) = 0;

    virtual void LogHttpRequest(struct http_server_request &request,
                                http_status_t status, off_t length,
                                uint64_t bytes_received, uint64_t bytes_sent) = 0;

    /**
     * A fatal protocol level error has occurred, and the connection
     * was closed.
     *
     * This will be called instead of HttpConnectionClosed().
     */
    virtual void HttpConnectionError(GError *error) = 0;

    virtual void HttpConnectionClosed() = 0;
};

#endif
