/*
 * Serve HTTP requests from another HTTP server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "handler.h"
#include "request.h"
#include "http-server.h"
#include "http-cache.h"
#include "uri-address.h"
#include "global.h"
#include "header-forward.h"

void
proxy_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    http_method_t method;
    istream_t body;
    struct strmap *headers;

    assert(!request2->body_consumed);

    /* send a request body? */

    if (http_server_request_has_body(request) &&
        response_dispatcher_wants_body(request2)) {
        /* a request with a body - reserve it for the processor, and
           convert this request to a GET */

        method = HTTP_METHOD_GET;
        body = NULL;
    } else {
        /* forward body (if any) to the real server */

        method = request->method;
        body = request->body;
        request2->body_consumed = true;
    }

    /* generate request headers */

    headers = forward_request_headers(request->pool, request->headers,
                                      request->remote_host, body != NULL,
                                      !request_processor_enabled(request2),
                                      NULL, NULL, NULL);

    /* do it */

    http_cache_request(global_http_cache, request->pool,
                       method, tr->address.u.http, headers, body,
                       &response_handler, request2,
                       request2->async_ref);
}
