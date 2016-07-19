/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_HTTP_SERVER_HANDLER_HXX
#define BENG_HTTP_SERVER_HANDLER_HXX

#include "glibfwd.hxx"

#include <http/status.h>

#include <stdint.h>

struct HttpServerRequest;
class CancellablePointer;

class HttpServerConnectionHandler {
public:
    virtual void HandleHttpRequest(HttpServerRequest &request,
                                   CancellablePointer &cancel_ptr) = 0;

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
    virtual void LogHttpRequest(HttpServerRequest &request,
                                http_status_t status, int64_t length,
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
