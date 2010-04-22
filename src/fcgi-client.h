/*
 * FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FCGI_CLIENT_H
#define __BENG_FCGI_CLIENT_H

#include "istream.h"
#include "http.h"

struct lease;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

/**
 * Sends a HTTP request on a socket to an FastCGI server, and passes
 * the response to the handler.
 *
 * @param pool the memory pool; this client holds a reference until
 * the response callback has returned and the response body is closed
 * @param fd a socket to the HTTP server
 * @param fd_type the exact socket type
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_filename the absolue path name of the script
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param document_root the absolute path of the document root
 * @param headers the serialized request headers (optional)
 * @param body the request body (optional)
 * @param handler a callback function which receives the response
 * @param ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
void
fcgi_client_request(pool_t pool, int fd, enum istream_direct fd_type,
                    const struct lease *lease, void *lease_ctx,
                    http_method_t method, const char *uri,
                    const char *script_filename,
                    const char *script_name, const char *path_info,
                    const char *query_string,
                    const char *document_root,
                    struct strmap *headers, istream_t body,
                    const char *const params[], unsigned num_params,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#endif
