/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "cgi.h"

void
cgi_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;

    pool_ref(request->pool);

    async_ref_clear(&request2->url_stream);

    cgi_new(request->pool, tr->path,
            request->method, request->uri,
            request->headers, request->body,
            &response_handler, request2,
            &request2->url_stream);
}
