/*
 * Web Application Socket client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CLIENT_H
#define BENG_PROXY_WAS_CLIENT_H

#include "istream.h"

#include <http/method.h>

struct lease;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

/**
 * Sends a HTTP request on a socket to a WAS server, and passes the
 * response to the handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param control_fd a control socket to the WAS server
 * @param input_fd a data pipe for the response body
 * @param output_fd a data pipe for the request body
 * @param lease the lease for both sockets
 * @param lease_ctx a context pointer for the lease
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param headers the request headers (optional)
 * @param body the request body (optional)
 * @param params application specific parameters
 * @param handler a callback function which receives the response
 * @param ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
void
was_client_request(pool_t pool, int control_fd, int input_fd, int output_fd,
                   const struct lease *lease, void *lease_ctx,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   struct strmap *headers, istream_t body,
                   const char *const params[], unsigned num_params,
                   const struct http_response_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

#endif
