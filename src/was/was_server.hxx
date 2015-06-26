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
struct istream;
struct lease;
struct strmap;
struct http_response_handler;
struct async_operation_ref;

struct was_server_handler {
    void (*request)(struct pool *pool, http_method_t method, const char *uri,
                    struct strmap *headers, struct istream *body, void *ctx);

    void (*free)(void *ctx);
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
struct was_server *
was_server_new(struct pool *pool, int control_fd, int input_fd, int output_fd,
               const struct was_server_handler *handler, void *handler_ctx);

void
was_server_free(struct was_server *server);

void
was_server_response(struct was_server *server, http_status_t status,
                    struct strmap *headers, struct istream *body);

#endif
