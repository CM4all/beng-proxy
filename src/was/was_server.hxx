/*
 * Web Application Socket server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_SERVER_HXX
#define BENG_PROXY_WAS_SERVER_HXX

#include <http/method.h>
#include <http/status.h>

struct pool;
class EventLoop;
class Istream;
struct lease;
class StringMap;
struct http_response_handler;
struct async_operation_ref;
struct WasServer;

class WasServerHandler {
public:
    virtual void OnWasRequest(struct pool &pool, http_method_t method,
                              const char *uri, StringMap &&headers,
                              Istream *body) = 0;

    virtual void OnWasClosed() = 0;
};

/**
 * Creates a WAS server, waiting for HTTP requests on the specified
 * socket.
 *
 * @param pool the memory pool
 * @param control_fd a control socket to the WAS client
 * @param input_fd a data pipe for the request body
 * @param output_fd a data pipe for the response body
 * @param handler a callback function which receives events
 * @param ctx a context pointer for the callback function
 */
WasServer *
was_server_new(struct pool &pool, EventLoop &event_loop,
               int control_fd, int input_fd, int output_fd,
               WasServerHandler &handler);

void
was_server_free(WasServer *server);

void
was_server_response(WasServer &server, http_status_t status,
                    StringMap &&headers, Istream *body);

#endif
