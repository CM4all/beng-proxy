/*
 * Serve HTTP requests from another HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "url-stream.h"

void
proxy_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;

    pool_ref(request->pool);

    url_stream_new(request->pool,
                   request2->http_client_stock,
                   request->method, tr->proxy, NULL,
                   request->content_length, request->body,
                   &response_handler, request2,
                   &request2->url_stream);
}
