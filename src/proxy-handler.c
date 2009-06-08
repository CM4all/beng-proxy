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

void
proxy_handler(struct request *request2)
{
    struct http_server_request *request = request2->request;
    const struct translate_response *tr = request2->translate.response;
    http_method_t method;
    struct strmap *headers;
    const char *p;
    istream_t body;

    headers = strmap_new(request->pool, 32);

    /* generate the "Via" request header */

    p = strmap_get(request->headers, "via");
    if (p == NULL) {
        if (request->remote_host != NULL)
            strmap_add(headers, "via",
                       p_strcat(request->pool, "1.1 ",
                                request->remote_host, NULL));
    } else {
        if (request->remote_host == NULL)
            strmap_add(headers, "via", p);
        else
            strmap_add(headers, "via",
                       p_strcat(request->pool, p, ", 1.1 ",
                                request->remote_host, NULL));
    }

    /* send a request body? */

    if (http_server_request_has_body(request) &&
        (response_dispatcher_wants_body(request2) || request2->body_consumed)) {
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

    /* do it */

    http_cache_request(global_http_cache, request->pool,
                       method, tr->address.u.http, headers, body,
                       &response_handler, request2,
                       request2->async_ref);
}
