/*
 * High level FastCGI client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fcgi-request.h"
#include "fcgi-stock.h"
#include "fcgi-client.h"
#include "http-response.h"
#include "socket-util.h"
#include "lease.h"

#include <daemon/log.h>

#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

struct fcgi_request {
    pool_t pool;
    http_method_t method;
    const char *uri;
    const char *script_name;
    const char *path_info;
    const char *query_string;
    const char *document_root;
    struct strmap *headers;
    istream_t body;

    struct http_response_handler_ref handler;
    struct async_operation_ref *async_ref;
};


/*
 * socket lease
 *
 */

static void
fcgi_socket_release(bool reuse __attr_unused, void *ctx)
{
    int fd = (int)(size_t)ctx;

    close(fd);
}

static const struct lease fcgi_socket_lease = {
    .release = fcgi_socket_release,
};


/*
 * constructor
 *
 */

void
fcgi_request(pool_t pool, struct fcgi_stock *fcgi_stock,
             const char *path,
             http_method_t method, const char *uri,
             const char *script_name, const char *path_info,
             const char *query_string,
             const char *document_root,
             struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             struct async_operation_ref *async_ref)
{
    const char *socket_path;
    /*struct fcgi_request *request;*/
    int fd;

    socket_path = fcgi_stock_get(fcgi_stock, path);
    if (socket_path == NULL) {
        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    /*
    pool_ref(pool);
    request = p_malloc(pool, sizeof(*request));
    request->pool = pool;
    request->method = method;
    request->uri = uri;
    request->script_name = script_name;
    request->path_info = path_info;
    request->query_string = query_string;
    request->document_root = document_root;
    request->headers = headers;
    request->body = body;
    http_response_handler_set(&request->handler, handler, handler_ctx);
    request->async_ref = async_ref;
    */

    fd = socket_unix_connect(socket_path);
    if (fd < 0) {
        daemon_log(2, "failed to connect to FastCGI server: %s\n",
                   strerror(errno));
        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    fcgi_client_request(pool, fd,
                        &fcgi_socket_lease, (void*)(size_t)fd,
                        method, uri,
                        script_name, path_info,
                        query_string,
                        document_root,
                        headers, body,
                        handler, handler_ctx, async_ref);
}
