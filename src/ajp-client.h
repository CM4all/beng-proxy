/*
 * AJPv13 client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_CLIENT_H
#define __BENG_AJP_CLIENT_H

#include "istream-direct.h"

#include <http/method.h>

#include <glib.h>

struct pool;
struct istream;
struct lease;
struct http_response_handler;
struct strmap;
struct async_operation_ref;

G_GNUC_CONST
static inline GQuark
ajp_client_quark(void)
{
    return g_quark_from_static_string("ajp_client");
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends a HTTP request on a socket to an AJPv13 server, and passes
 * the response to the handler.
 *
 * @param pool the memory pool
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param protocol the name of the original protocol, e.g. "http"
 * @param remote_addr the address of the original client
 * @param remote_host the host name of the original client
 * @param server_name the host name of the server
 * @param server_port the port to which the client connected
 * @param is_ssl true if the client is using SSL
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param handler a callback function which receives the response
 * @param ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
void
ajp_client_request(struct pool *pool, int fd, enum istream_direct fd_type,
                   const struct lease *lease, void *lease_ctx,
                   const char *protocol, const char *remote_addr,
                   const char *remote_host, const char *server_name,
                   unsigned server_port, bool is_ssl,
                   http_method_t method, const char *uri,
                   struct strmap *headers,
                   struct istream *body,
                   const struct http_response_handler *handler,
                   void *ctx,
                   struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
