/*
 * HTTP server implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_HTTP_SERVER_REQUEST_HXX
#define BENG_HTTP_SERVER_REQUEST_HXX

#include "net/SocketAddress.hxx"

#include <http/method.h>

struct pool;
struct istream;

struct http_server_request {
    struct pool *pool;
    struct http_server_connection *connection;

    SocketAddress local_address, remote_address;

    /**
     * The local address (host and port) that was connected to.
     */
    const char *local_host_and_port;

    /**
     * The address (host and port) of the client.
     */
    const char *remote_host_and_port;

    /**
     * The address of the client, without the port number.
     */
    const char *remote_host;

    /* request metadata */
    http_method_t method;
    char *uri;
    struct strmap *headers;

    /**
     * The request body.  The handler is responsible for closing this
     * istream.
     */
    struct istream *body;

    bool HasBody() const {
        return body != nullptr;
    }
};

#endif
