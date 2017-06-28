/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_CLIENT_HXX
#define BENG_PROXY_HTTP_CLIENT_HXX

#include "io/FdType.hxx"

#include <http/method.h>

#include <glib.h>

#include <stdexcept>

struct pool;
class EventLoop;
class Istream;
class SocketDescriptor;
class Lease;
struct SocketFilter;
class HttpResponseHandler;
class CancellablePointer;
class HttpHeaders;

/**
 * GError codes for http_client_quark().
 */
enum class HttpClientErrorCode {
    UNSPECIFIED,

    /**
     * The server has closed the connection before the first response
     * byte.
     */
    REFUSED,

    /**
     * The server has closed the connection prematurely.
     */
    PREMATURE,

    /**
     * A socket I/O error has occurred.
     */
    IO,

    /**
     * Non-HTTP garbage was received.
     */
    GARBAGE,

    /**
     * The server has failed to respond or accept data in time.
     */
    TIMEOUT,
};

class HttpClientError : public std::runtime_error {
    HttpClientErrorCode code;

public:
    HttpClientError(HttpClientErrorCode _code, const char *_msg)
        :std::runtime_error(_msg), code(_code) {}

    HttpClientErrorCode GetCode() const {
        return code;
    }
};

G_GNUC_CONST
static inline GQuark
http_client_quark(void)
{
    return g_quark_from_static_string("http_client");
}

/**
 * Is the specified error a server failure, that justifies
 * blacklisting the server for a while?
 */
bool
IsHttpClientServerFailure(std::exception_ptr ep);

static inline bool
IsHttpClientServerFailure(const GError &error)
{
    return error.domain == http_client_quark() &&
        HttpClientErrorCode(error.code) != HttpClientErrorCode::UNSPECIFIED;
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
 * @param handler receives the response
 * @param async_ref a handle which may be used to abort the operation
 */
void
http_client_request(struct pool &pool, EventLoop &event_loop,
                    SocketDescriptor fd, FdType fd_type,
                    Lease &lease,
                    const char *peer_name,
                    const SocketFilter *filter, void *filter_ctx,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    Istream *body, bool expect_100,
                    HttpResponseHandler &handler,
                    CancellablePointer &cancel_ptr);

#endif
