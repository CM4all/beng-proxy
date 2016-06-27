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
class StringMap;
class Istream;
struct HttpServerConnection;

struct HttpServerRequest {
    struct pool *pool;
    HttpServerConnection *connection;

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
    StringMap *headers;

    /**
     * The request body.  The handler is responsible for closing this
     * istream.
     */
    Istream *body;

    HttpServerRequest(struct pool &_pool, HttpServerConnection &_connection,
                      SocketAddress _local_address,
                      SocketAddress _remote_address,
                      const char *_local_host_and_port,
                      const char *_remote_host_and_port,
                      const char *_remote_host);

    HttpServerRequest(const HttpServerRequest &) = delete;
    HttpServerRequest &operator=(const HttpServerRequest &) = delete;

    bool HasBody() const {
        return body != nullptr;
    }
};

#endif
