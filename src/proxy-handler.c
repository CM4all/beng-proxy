/*
 * Serve HTTP requests from the local VFS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"

void
proxy_callback(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;

    pool_ref(request->pool);

    request2->url_stream = url_stream_new(request->pool,
                                          request->method, tr->proxy, NULL,
                                          request->content_length, request->body,
                                          &response_handler, request2);
    if (request2->url_stream == NULL) {
        pool_unref(request->pool);
        http_server_send_message(request,
                                 HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
    }
}
