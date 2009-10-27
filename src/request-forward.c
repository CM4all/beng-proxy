/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request-forward.h"
#include "request.h"
#include "http-server.h"
#include "header-forward.h"

void
request_forward(struct forward_request *dest, struct request *request2,
                const char *host_and_port, const char *uri)
{
    struct http_server_request *request = request2->request;
    struct session *session;

    assert(!request2->body_consumed);

    /* send a request body? */

    if (request2->processor_focus) {
        /* reserve method+body for the processor, and
           convert this request to a GET */

        dest->method = HTTP_METHOD_GET;
        dest->body = NULL;
    } else {
        /* forward body (if any) to the real server */

        dest->method = request->method;
        dest->body = request->body;
        request2->body_consumed = true;
    }

    /* generate request headers */

    session = request_get_session(request2);
    dest->headers = forward_request_headers(request->pool, request->headers,
                                            request->local_host,
                                            request->remote_host,
                                            dest->body != NULL,
                                            !request_processor_enabled(request2),
                                            session, host_and_port, uri);
    if (session != NULL)
        session_put(session);
}
