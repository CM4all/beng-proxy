/*
 * Web Application Socket client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_CLIENT_HXX
#define BENG_PROXY_WAS_CLIENT_HXX

#include <http/method.h>

struct pool;
class EventLoop;
class Istream;
class WasLease;
class StringMap;
class HttpResponseHandler;
class CancellablePointer;
template<typename T> struct ConstBuffer;

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
 * @param method the HTTP request method
 * @param uri the request URI path
 * @param script_name the URI part of the script
 * @param path_info the URI part following the script name
 * @param query_string the query string (without the question mark)
 * @param headers the request headers (optional)
 * @param body the request body (optional)
 * @param params application specific parameters
 * @param handler receives the response
 * @param cancel_ptr a handle which may be used to abort the operation
 */
void
was_client_request(struct pool &pool, EventLoop &event_loop,
                   int control_fd, int input_fd, int output_fd,
                   WasLease &lease,
                   http_method_t method, const char *uri,
                   const char *script_name, const char *path_info,
                   const char *query_string,
                   StringMap &headers, Istream *body,
                   ConstBuffer<const char *> params,
                   HttpResponseHandler &handler,
                   CancellablePointer &cancel_ptr);

#endif
