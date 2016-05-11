/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CLIENT_HXX
#define BENG_PROXY_HTTP_CLIENT_HXX

#include "FdType.hxx"

#include <http/method.h>

#include <glib.h>

struct pool;
class EventLoop;
class Istream;
class Lease;
struct SocketFilter;
struct http_response_handler;
struct async_operation_ref;
class HttpHeaders;

/**
 * GError codes for http_client_quark().
 */
enum http_client_error {
    HTTP_CLIENT_UNSPECIFIED,

    /**
     * The server has closed the connection before the first response
     * byte.
     */
    HTTP_CLIENT_REFUSED,

    /**
     * The server has closed the connection prematurely.
     */
    HTTP_CLIENT_PREMATURE,

    /**
     * A socket I/O error has occurred.
     */
    HTTP_CLIENT_IO,

    /**
     * Non-HTTP garbage was received.
     */
    HTTP_CLIENT_GARBAGE,

    /**
     * The server has failed to respond or accept data in time.
     */
    HTTP_CLIENT_TIMEOUT,
};

G_GNUC_CONST
static inline GQuark
http_client_quark(void)
{
    return g_quark_from_static_string("http_client");
}

/**
 * Sends a HTTP request on a socket, and passes the response to the
 * handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param expect_100 true to send "Expect: 100-continue" in the
 * presence of a request body
 * @param handler a callback function which receives the response
 * @param ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
void
http_client_request(struct pool &pool, EventLoop &event_loop,
                    int fd, FdType fd_type,
                    Lease &lease,
                    const char *peer_name,
                    const SocketFilter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    Istream *body, bool expect_100,
                    const struct http_response_handler &handler,
                    void *ctx,
                    struct async_operation_ref &async_ref);

#endif
