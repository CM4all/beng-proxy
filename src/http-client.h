/*
 * HTTP client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTTP_CLIENT_H
#define __BENG_HTTP_CLIENT_H

#include "pool.h"
#include "http.h"
#include "istream.h"

struct lease;
struct growing_buffer;
struct http_response_handler;
struct async_operation_ref;

/**
 * Sends a HTTP request on a socket, and passes the response to the
 * handler.
 *
 * @param pool the memory pool
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param handler a callback function which receives the response
 * @param ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
void
http_client_request(pool_t pool, int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    struct growing_buffer *headers,
                    istream_t body,
                    const struct http_response_handler *handler,
                    void *ctx,
                    struct async_operation_ref *async_ref);

#endif
