/*
 * Common request forwarding code for the request handlers.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request_forward.hxx"
#include "request.hxx"
#include "http_server.hxx"
#include "header_forward.hxx"

void
request_forward(struct forward_request *dest, struct request *request2,
                const struct header_forward_settings *header_forward,
                const char *host_and_port, const char *uri,
                bool exclude_host)
{
    struct http_server_request *request = request2->request;
    struct session *session;

    assert(!http_server_request_has_body(request) ||
           request2->body != nullptr);

    /* send a request body? */

    if (request2->processor_focus) {
        /* reserve method+body for the processor, and
           convert this request to a GET */

        dest->method = HTTP_METHOD_GET;
        dest->body = nullptr;
    } else {
        /* forward body (if any) to the real server */

        dest->method = request->method;
        dest->body = request2->body;
        request2->body = nullptr;
    }

    /* generate request headers */

    session = request_get_session(request2);
    dest->headers = forward_request_headers(request->pool, request->headers,
                                            request->local_host_and_port,
                                            request->remote_host,
                                            exclude_host,
                                            dest->body != nullptr,
                                            !request_processor_enabled(request2),
                                            !request_transformation_enabled(request2),
                                            header_forward,
                                            session, host_and_port, uri);
    if (session != nullptr)
        session_put(session);
}
